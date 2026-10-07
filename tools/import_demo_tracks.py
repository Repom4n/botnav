#!/usr/bin/env python3
"""Import version-1 decoded snapshot JSON with Python 3.9+, never raw .dm_26.

Samples are authoritative positions, not reconstructed user commands. Chains
use a conservative fixed heading (24-unit strip); individual jumps use a
48-unit strip. Ground contacts connecting jumps must be within 48 units and
200 ms. Teleport detection is necessarily heuristic without event metadata.
Flight must show rising position or positive vertical velocity: this excludes
plain falls, but cannot distinguish a jump from rising knockback in this schema.
"""

import argparse
import json
import math
import os
from pathlib import Path
import re
import sys
import uuid
from dataclasses import dataclass


MAX_INPUT_BYTES = 64 * 1024 * 1024
MAX_FRAMES = 250000
MAX_TRACKS = 64
MAX_ROUTES = 64
MAX_GAP_MS = 200
MAX_DURATION_MS = 10000
MIN_LENGTH = 512
MAX_LENGTH = 2048
MAX_COORDINATE = 131072
MAX_QPATH = 64


class ImportError(ValueError):
    """Invalid input or an import that cannot safely produce output."""


@dataclass(frozen=True)
class Frame:
    time_ms: int
    origin: tuple
    velocity: tuple
    grounded: bool
    alive: bool


@dataclass(frozen=True)
class Route:
    start: tuple
    end: tuple
    min_speed: float


def validate_map(value):
    if not isinstance(value, str):
        raise ImportError("map must be a safe relative ASCII path, e.g. mp/ffa1")
    if len(value) + len("botroutes/.demoroute") >= MAX_QPATH:
        raise ImportError("botroutes/<map>.demoroute must fit the engine's 64-byte path limit")
    if not re.fullmatch(r"[A-Za-z0-9_-]+(?:/[A-Za-z0-9_-]+)*", value):
        raise ImportError("map must be a safe relative ASCII path, e.g. mp/ffa1")
    return value


def _integer(value, low, high, label):
    if type(value) is not int or not low <= value <= high:
        raise ImportError(f"{label} must be an integer in [{low}, {high}]")
    return value


def _vector(value, bound, label):
    if not isinstance(value, list) or len(value) != 3:
        raise ImportError(f"{label} must have three numeric components")
    for number in value:
        if (type(number) not in (int, float)
                or not -bound <= number <= bound or not math.isfinite(number)):
            raise ImportError(f"{label} components must be finite and within +/-{bound}")
    return tuple(float(number) for number in value)


def validate_document(document):
    if not isinstance(document, dict) or set(document) != {"version", "map", "tracks"}:
        raise ImportError("expected exactly version, map and tracks")
    _integer(document["version"], 1, 1, "version")
    map_name = validate_map(document["map"])
    tracks = document["tracks"]
    if not isinstance(tracks, list) or not 1 <= len(tracks) <= MAX_TRACKS:
        raise ImportError(f"tracks must contain 1..{MAX_TRACKS} tracks")
    count = 0
    clients = set()
    normalized = []
    for track in tracks:
        if not isinstance(track, dict) or set(track) != {"client", "frames"}:
            raise ImportError("each track needs exactly client and frames")
        client = _integer(track["client"], 0, 63, "client")
        if client in clients:
            raise ImportError("duplicate client track")
        clients.add(client)
        frames = track["frames"]
        if not isinstance(frames, list):
            raise ImportError("frames must be an array")
        count += len(frames)
        if count > MAX_FRAMES:
            raise ImportError(f"input exceeds {MAX_FRAMES} total frames")
        previous_time = -1
        result = []
        for frame in frames:
            if (not isinstance(frame, dict)
                    or set(frame) != {"time_ms", "origin", "velocity", "grounded", "alive"}):
                raise ImportError("frame needs time_ms, origin, velocity, grounded and alive")
            time_ms = _integer(frame["time_ms"], 0, 2147483647, "time_ms")
            if time_ms <= previous_time:
                raise ImportError("frame times must be strictly increasing within each track")
            previous_time = time_ms
            if type(frame["grounded"]) is not bool or type(frame["alive"]) is not bool:
                raise ImportError("grounded and alive must be booleans")
            result.append(Frame(time_ms, _vector(frame["origin"], MAX_COORDINATE, "origin"),
                                _vector(frame["velocity"], 10000, "velocity"),
                                frame["grounded"], frame["alive"]))
        normalized.append((client, result))
    return map_name, sorted(normalized)


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ImportError(f"duplicate JSON field: {key}")
        result[key] = value
    return result


def _reject_constant(value):
    raise ImportError(f"non-finite JSON number: {value}")


def load_tracks(path):
    with open(path, "rb") as stream:
        raw = stream.read(MAX_INPUT_BYTES + 1)
    if len(raw) > MAX_INPUT_BYTES:
        raise ImportError("input exceeds 64 MiB")
    try:
        document = json.loads(raw.decode("utf-8"), object_pairs_hook=_unique_object,
                              parse_constant=_reject_constant)
    except (UnicodeError, ValueError, RecursionError) as error:
        raise ImportError(f"invalid decoded snapshot JSON: {error}") from error
    return validate_document(document)


def _length(start, end):
    return math.hypot(end[0] - start[0], end[1] - start[1])


def _continuous(first, second):
    gap = second.time_ms - first.time_ms
    if not first.alive or not second.alive or gap > MAX_GAP_MS:
        return False
    distance = math.dist(first.origin, second.origin)
    speed = max(math.hypot(*first.velocity), math.hypot(*second.velocity))
    # Allow snapshot quantization/acceleration, but never plausible warp speeds.
    return distance <= 64 + min(4000, speed * 1.5) * gap / 1000


def _in_strip(points, start, end, width):
    dx, dy = end[0] - start[0], end[1] - start[1]
    length = math.hypot(dx, dy)
    if length == 0:
        return False
    furthest = 0
    for frame in points:
        x, y = frame.origin[0] - start[0], frame.origin[1] - start[1]
        along = (x * dx + y * dy) / length
        if (abs(x * dy - y * dx) / length > width
                or along < furthest - width or along > length + width):
            return False
        furthest = max(furthest, along)
    return True


def _jumps(frames):
    """Yield complete jumps and a continuity epoch, scanning each sample once."""
    previous = None
    launch = None
    airborne = []
    rising = False
    ground_since = None
    ground_distance = 0
    ground_points = []
    epoch = 0
    for frame in frames:
        if previous is not None and not _continuous(previous, frame):
            launch, airborne = None, []
            rising = False
            ground_since, ground_distance = None, 0
            ground_points = []
            epoch += 1
        if not frame.alive:
            launch, airborne = None, []
            rising = False
            ground_since, ground_distance = None, 0
            ground_points = []
        elif frame.grounded:
            if launch is not None and airborne:
                points = [launch] + airborne + [frame]
                length = _length(launch.origin, frame.origin)
                if (rising and frame.time_ms - launch.time_ms <= MAX_DURATION_MS
                        and 0 < length <= MAX_LENGTH
                        and abs(frame.origin[2] - launch.origin[2]) <= 32
                        and _in_strip(points, launch.origin, frame.origin, 48)):
                    yield epoch, points, ground_points
                else:
                    epoch += 1
                ground_since, ground_distance = frame, 0
                ground_points = [frame]
            elif ground_since is not None and previous is not None:
                ground_distance += math.dist(previous.origin, frame.origin)
                ground_points.append(frame)
                if (ground_distance > 48
                        or frame.time_ms - ground_since.time_ms > MAX_GAP_MS):
                    epoch += 1
                    ground_since, ground_distance = frame, 0
                    ground_points = [frame]
            else:
                ground_since, ground_distance = frame, 0
                ground_points = [frame]
            launch, airborne, rising = frame, [], False
        elif launch is not None:
            ground_since, ground_distance = None, 0
            if (frame.velocity[2] > 0
                    or (previous is not None and frame.origin[2] > previous.origin[2])):
                rising = True
            airborne.append(frame)
            if frame.time_ms - launch.time_ms > MAX_DURATION_MS:
                launch, airborne = None, []
                epoch += 1
        previous = frame


def extract_routes(tracks):
    """Return at most 64 deterministic, directed corridors and diagnostics.

    Greedy chains emit on reaching 512 units. Each jump is checked once alone
    and once for joining a fixed-heading chain, avoiding quadratic rescanning.
    """
    routes = []
    duplicates = capped = 0
    for _client, frames in sorted(tracks):
        chain = []
        launches = []
        heading = None
        chain_epoch = None
        for epoch, jump, connector in _jumps(frames):
            start, end = jump[0], jump[-1]
            join = bool(chain) and epoch == chain_epoch
            if join:
                anchor = chain[0]
                dx, dy = heading
                axis_end = (anchor.origin[0] + dx * MAX_LENGTH,
                            anchor.origin[1] + dy * MAX_LENGTH, anchor.origin[2])
                join = (start.time_ms - chain[-1].time_ms <= MAX_GAP_MS
                        and math.dist(start.origin, chain[-1].origin) <= 48
                        and end.time_ms - anchor.time_ms <= MAX_DURATION_MS
                        and _length(anchor.origin, end.origin) <= MAX_LENGTH
                        and abs(end.origin[2] - anchor.origin[2]) <= 32
                        and _in_strip(connector + jump, anchor.origin, axis_end, 24))
            if not join:
                chain, launches = [], []
                length = _length(start.origin, end.origin)
                heading = ((end.origin[0] - start.origin[0]) / length,
                           (end.origin[1] - start.origin[1]) / length)
                chain_epoch = epoch
            else:
                chain.extend(connector)
            chain.extend(jump)
            launches.append(math.hypot(start.velocity[0], start.velocity[1]))
            if _length(chain[0].origin, end.origin) >= MIN_LENGTH:
                valid = _in_strip(chain, chain[0].origin, end.origin, 48)
                if not valid:
                    # Keep the newest complete jump when an older prefix bends.
                    chain = list(jump)
                    launches = [launches[-1]]
                    length = _length(start.origin, end.origin)
                    heading = ((end.origin[0] - start.origin[0]) / length,
                               (end.origin[1] - start.origin[1]) / length)
                    chain_epoch = epoch
                    valid = length >= MIN_LENGTH
                if valid:
                    route = Route(chain[0].origin, end.origin,
                                  min(2000.0, min(launches) * 0.8))
                    if any(math.dist(route.start, old.start) <= 64
                           and math.dist(route.end, old.end) <= 64 for old in routes):
                        duplicates += 1
                    elif len(routes) < MAX_ROUTES:
                        routes.append(route)
                    else:
                        capped += 1
                    chain, launches = [], []
    diagnostics = [f"extracted {len(routes)} routes; deduplicated {duplicates}; "
                   f"discarded {capped} beyond the {MAX_ROUTES}-route cap"]
    return routes, diagnostics


def render_routes(routes):
    def vector(position):
        return " ".join(format(value, ".9g") for value in position)
    return "".join("strafejump\n{\n"
                   f"\tstart_pos {vector(route.start)}\n"
                   f"\tend_pos {vector(route.end)}\n"
                   f"\tmin_speed {route.min_speed:.9g}\n"
                   "}\n" for route in routes)


def write_routes(output_dir, map_name, routes, force=False):
    validate_map(map_name)
    if not routes:
        raise ImportError("no successful jump corridors found; no output written")
    root = Path(output_dir).resolve()
    target = root / "botroutes" / (map_name + ".demoroute")
    if not target.resolve().is_relative_to(root):
        raise ImportError("output path escapes the game directory")
    if target.exists() and not force:
        raise ImportError(f"output already exists: {target}; use --force to replace")
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.parent.resolve().is_relative_to(root):
        raise ImportError("output directory escapes the game directory")
    temporary = target.parent / (target.name + "." + uuid.uuid4().hex + ".pending")
    try:
        with open(temporary, "x", encoding="ascii", newline="\n") as stream:
            stream.write(render_routes(routes))
            stream.flush()
            os.fsync(stream.fileno())
        if force:
            os.replace(temporary, target)
        else:
            # A hard link publishes a complete file without a check/write race.
            try:
                os.link(temporary, target)
            except FileExistsError as error:
                raise ImportError(f"output already exists: {target}; use --force") from error
    finally:
        temporary.unlink(missing_ok=True)
    return target


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="version-1 decoded snapshot JSON (not .dm_26)")
    parser.add_argument("--output-dir", required=True, help="game directory")
    parser.add_argument("--force", action="store_true", help="replace an existing output")
    args = parser.parse_args(argv)
    try:
        map_name, tracks = load_tracks(args.input)
        routes, diagnostics = extract_routes(tracks)
        target = write_routes(args.output_dir, map_name, routes, args.force)
    except (ImportError, OSError) as error:
        parser.exit(1, f"error: {error}\n")
    for diagnostic in diagnostics:
        print(diagnostic, file=sys.stderr)
    print(target)
    return 0


if __name__ == "__main__":
    sys.exit(main())
