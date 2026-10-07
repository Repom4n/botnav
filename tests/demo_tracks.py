#!/usr/bin/env python3
"""Engine-free tests for the normalized demo-track importer."""

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import unittest
import tempfile
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
SCRIPT = ROOT / "tools" / "import_demo_tracks.py"
SPEC = importlib.util.spec_from_file_location("import_demo_tracks", SCRIPT)
importer = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = importer
SPEC.loader.exec_module(importer)


def frame(time, x, y=0, z=0, grounded=False, alive=True, speed=600):
    return {"time_ms": time, "origin": [x, y, z], "velocity": [speed, 0, 0],
            "grounded": grounded, "alive": alive}


def jump(x=0, y=0, time=0, length=600, speed=600):
    return [frame(time + index * 100, x + length * index / 10, y,
                  0 if index in (0, 10) else 48,
                  index in (0, 10), speed=speed) for index in range(11)]


def document(frames=None):
    return {"version": 1, "map": "mp/ffa1",
            "tracks": [{"client": 0, "frames": frames if frames is not None else jump()}]}


def routes(frames):
    _, tracks = importer.validate_document(document(frames))
    return importer.extract_routes(tracks)[0]


class ValidationTests(unittest.TestCase):
    def test_document_and_numbers(self):
        good = document()
        name, tracks = importer.validate_document(good)
        self.assertEqual(name, "mp/ffa1")
        self.assertEqual(len(tracks[0][1]), 11)
        mutations = [
            lambda d: d.update(version=True),
            lambda d: d.update(version=2),
            lambda d: d.update(extra="not metadata"),
            lambda d: d.update(tracks={}),
            lambda d: d["tracks"][0].update(client=True),
            lambda d: d["tracks"][0].update(client=64),
            lambda d: d["tracks"][0].update(frames={}),
            lambda d: d["tracks"][0]["frames"][0].update(time_ms=0.5),
            lambda d: d["tracks"][0]["frames"][0].update(time_ms=-1),
            lambda d: d["tracks"][0]["frames"][0].update(time_ms=2147483648),
            lambda d: d["tracks"][0]["frames"][0].update(grounded=1),
            lambda d: d["tracks"][0]["frames"][0].update(alive="true"),
            lambda d: d["tracks"][0]["frames"][0].update(origin=[0, 0]),
            lambda d: d["tracks"][0]["frames"][0].update(origin=[True, 0, 0]),
            lambda d: d["tracks"][0]["frames"][0].update(origin=[131073, 0, 0]),
            lambda d: d["tracks"][0]["frames"][0].update(velocity=[10001, 0, 0]),
            lambda d: d["tracks"][0]["frames"][0].update(origin=[float("nan"), 0, 0]),
            lambda d: d["tracks"][0]["frames"][0].update(velocity=[float("inf"), 0, 0]),
        ]
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                bad = copy.deepcopy(good)
                mutate(bad)
                with self.assertRaises(importer.ImportError):
                    importer.validate_document(bad)

    def test_times_and_track_limits(self):
        for time in (0, -1, 100):
            bad = document()
            bad["tracks"][0]["frames"][2]["time_ms"] = time
            with self.assertRaises(importer.ImportError):
                importer.validate_document(bad)
        bad = document()
        bad["tracks"] *= 2
        with self.assertRaises(importer.ImportError):
            importer.validate_document(bad)
        bad["tracks"] *= 33
        with self.assertRaises(importer.ImportError):
            importer.validate_document(bad)
        with patch.object(importer, "MAX_FRAMES", 10):
            with self.assertRaises(importer.ImportError):
                importer.validate_document(document())

    def test_map_safety(self):
        for name in ("../ffa1", "mp/../ffa1", "/mp/ffa1", "mp\\ffa1", "mp//ffa1",
                     "mp/ffa1.", "mp/.ffa1", "C:/ffa1", "mp/é", "mp/ffa1\n",
                     "mp/ffa 1", "", "a" * 129, None):
            with self.subTest(name=name), self.assertRaises(importer.ImportError):
                importer.validate_map(name)
        self.assertEqual(importer.validate_map("mp/ffa1"), "mp/ffa1")
        self.assertEqual(importer.validate_map("a" * 43), "a" * 43)
        with self.assertRaises(importer.ImportError):
            importer.validate_map("a" * 44)

    def test_runtime_coordinate_bounds(self):
        doc = document()
        doc["tracks"][0]["frames"][0]["origin"] = [-131072, 131072, 131072]
        importer.validate_document(doc)
        for value in (-131073, 131073):
            doc["tracks"][0]["frames"][0]["origin"][0] = value
            with self.assertRaises(importer.ImportError):
                importer.validate_document(doc)


class ExtractionTests(unittest.TestCase):
    def test_jump_and_grounded_speed(self):
        frames = jump()
        for sample in frames[1:-1]:
            sample["velocity"] = [1500, 0, 0]
        result = routes(frames)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0].start, (0, 0, 0))
        self.assertEqual(result[0].end, (600, 0, 0))
        self.assertEqual(result[0].min_speed, 480)
        self.assertIn("strafejump\n{\n", importer.render_routes(result))
        self.assertEqual(routes(jump(speed=10000))[0].min_speed, 2000)
        frames = jump()
        frames[0]["velocity"] = [0, 0, 0]
        self.assertEqual(routes(frames)[0].min_speed, 0)

    def test_completed_and_level_only(self):
        self.assertFalse(routes(jump()[1:]))
        self.assertFalse(routes(jump()[:-1]))
        raised = jump()
        raised[-1]["origin"][2] = 33
        self.assertFalse(routes(raised))
        long = jump(length=2100, speed=2200)
        self.assertFalse(routes(long))
        slow = jump()
        for sample in slow:
            sample["time_ms"] *= 11
        self.assertFalse(routes(slow))
        slow = [frame(index * 110, index * 6, z=0 if index in (0, 100) else 48,
                      grounded=index in (0, 100)) for index in range(101)]
        self.assertFalse(routes(slow))
        self.assertEqual(len(routes(jump(length=512))), 1)
        self.assertEqual(len(routes(jump(length=2048, speed=2200))), 1)
        boundary = jump()
        for sample in boundary:
            sample["time_ms"] *= 2
        self.assertEqual(len(routes(boundary)), 1)

    def test_rising_flight_heuristic(self):
        falling = jump()
        for index, sample in enumerate(falling):
            sample["origin"][2] = -index * 2
            sample["velocity"][2] = -20
        self.assertFalse(routes(falling))
        self.assertEqual(len(routes(jump())), 1)  # Position ascent alone suffices.
        positive_velocity = jump()
        for sample in positive_velocity:
            sample["origin"][2] = 0
            if not sample["grounded"]:
                sample["velocity"][2] = 100
        self.assertEqual(len(routes(positive_velocity)), 1)
        for sample in positive_velocity:
            sample["velocity"][2] = 0
        self.assertFalse(routes(positive_velocity))
        # A rejected falling prefix must not lose the next observed rising jump.
        suffix = jump(x=600, time=1100)
        self.assertEqual(routes(falling + suffix)[0].start, (600, 0, 0))

    def test_walk_and_turns(self):
        walk = jump()
        for sample in walk:
            sample["grounded"] = True
        self.assertFalse(routes(walk))
        turn = jump()
        turn[5]["origin"][1] = 49
        self.assertFalse(routes(turn))
        turn[5]["origin"][1] = 48
        self.assertEqual(len(routes(turn)), 1)
        reverse = jump()
        reverse[5]["origin"][0] = -60
        self.assertFalse(routes(reverse))
        reverse = jump()
        reverse[4]["origin"][0] = 120
        self.assertFalse(routes(reverse))

    def test_breaks_preserve_valid_suffix(self):
        for kind in ("gap", "death", "teleport", "turn"):
            broken = jump()
            if kind == "gap":
                for sample in broken[5:]:
                    sample["time_ms"] += 201
            elif kind == "death":
                broken[5]["alive"] = False
            elif kind == "teleport":
                broken[5]["origin"][0] += 1500
            else:
                broken[5]["origin"][1] += 100
            suffix = jump(x=2000, time=broken[-1]["time_ms"] + 100)
            result = routes(broken + suffix)
            self.assertEqual(len(result), 1, kind)
            self.assertEqual(result[0].start[0], 2000, kind)

    def test_chains_and_ground_connectors(self):
        first = jump(length=300)
        second = jump(x=300, time=1000, length=300)
        result = routes(first + second[1:])
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0].end[0], 600)
        # Two distinct ground snapshots are allowed between hops.
        second = jump(x=310, time=1100, length=300)
        self.assertEqual(len(routes(first + second)), 1)
        # Do not use walked ground or a long pause to bridge short jumps.
        second = jump(x=400, time=1100, length=300)
        self.assertFalse(routes(first + second))
        second = jump(x=300, time=1300, length=300)
        connector = [frame(1100, 300, grounded=True),
                     frame(1200, 300, grounded=True)]
        self.assertFalse(routes(first + connector + second))
        second = jump(x=300, time=1200, length=300)
        connector = [frame(1100, 350, grounded=True)]
        self.assertFalse(routes(first + connector + second))
        second = jump(x=300, time=1100, length=300)
        second[3]["alive"] = False
        self.assertFalse(routes(first + second))

    def test_chain_turn_preserves_later_jump(self):
        first = jump(length=300)
        next_jump = jump(x=300, time=1100, length=600)
        for index, sample in enumerate(next_jump):
            sample["origin"][0] = 300
            sample["origin"][1] = index * 60
            sample["velocity"] = [0, 600, 0]
        result = routes(first + next_jump)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0].start, (300, 0, 0))
        self.assertEqual(result[0].end, (300, 600, 0))

    def test_dedup_cap_and_determinism(self):
        samples = []
        for index in range(70):
            samples += jump(y=index * 150, time=index * 1200)
        samples += jump(time=85000)
        name, tracks = importer.validate_document(document(samples))
        result, diagnostics = importer.extract_routes(tracks)
        self.assertEqual(len(result), 64)
        self.assertIn("deduplicated 1", diagnostics[0])
        self.assertIn("discarded 6", diagnostics[0])
        self.assertEqual(importer.extract_routes(tracks), (result, diagnostics))
        doc = document(jump())
        doc["tracks"].append({"client": 1, "frames": jump(y=150)})
        _, forward = importer.validate_document(doc)
        doc["tracks"].reverse()
        _, backward = importer.validate_document(doc)
        self.assertEqual(importer.extract_routes(forward), importer.extract_routes(backward))


class FileAndCliTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory(prefix="botnav-demo-track-")
        self.addCleanup(scratch.cleanup)
        self.directory = Path(scratch.name)
        self.input = self.directory / "decoded.json"
        self.input.write_text(json.dumps(document()), encoding="utf-8")
        self.output = self.directory / "game"

    def cli(self, *arguments):
        return subprocess.run([sys.executable, str(SCRIPT), *map(str, arguments)],
                              capture_output=True, text=True, check=False)

    def test_atomic_write_and_overwrite(self):
        result = routes(jump())
        target = importer.write_routes(self.output, "mp/ffa1", result)
        expected = importer.render_routes(result)
        self.assertEqual(target.read_text(), expected)
        with self.assertRaises(importer.ImportError):
            importer.write_routes(self.output, "mp/ffa1", result)
        self.assertEqual(target.read_text(), expected)
        importer.write_routes(self.output, "mp/ffa1", result, force=True)
        self.assertEqual(target.read_text(), expected)
        self.assertFalse(list(self.output.rglob("*.pending")))

    def test_atomic_failure_cleanup(self):
        result = routes(jump())
        with patch.object(importer.os, "link", side_effect=FileExistsError):
            with self.assertRaises(importer.ImportError):
                importer.write_routes(self.output, "mp/ffa1", result)
        self.assertFalse(list(self.output.rglob("*.pending")))
        self.assertFalse(list(self.output.rglob("*.demoroute")))
        target = importer.write_routes(self.output, "mp/ffa1", result)
        previous = target.read_bytes()
        with patch.object(importer.os, "replace", side_effect=OSError("write failed")):
            with self.assertRaises(OSError):
                importer.write_routes(self.output, "mp/ffa1", result, force=True)
        self.assertEqual(target.read_bytes(), previous)
        self.assertFalse(list(self.output.rglob("*.pending")))

    def test_no_routes_and_invalid_input_no_output(self):
        with self.assertRaises(importer.ImportError):
            importer.write_routes(self.output, "mp/ffa1", [])
        self.assertFalse(self.output.exists())
        for data in ('{"version":1,"version":1}', '{"x":NaN}', "not a demo",
                     '{"x":1e999}', '{"x":' + "[" * 1500):
            self.input.write_text(data, encoding="utf-8")
            result = self.cli(self.input, "--output-dir", self.output)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("error:", result.stderr)
            self.assertFalse(self.output.exists())
        self.input.write_text(json.dumps(document([])), encoding="utf-8")
        result = self.cli(self.input, "--output-dir", self.output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no successful", result.stderr)
        self.assertFalse(self.output.exists())

    def test_size_limit(self):
        with patch.object(importer, "MAX_INPUT_BYTES", 10):
            with self.assertRaises(importer.ImportError):
                importer.load_tracks(self.input)

    def test_output_symlink_escape(self):
        self.output.mkdir()
        outside = self.directory / "outside"
        outside.mkdir()
        (self.output / "botroutes").symlink_to(outside, target_is_directory=True)
        with self.assertRaises(importer.ImportError):
            importer.write_routes(self.output, "mp/ffa1", routes(jump()), force=True)
        self.assertEqual(list(outside.iterdir()), [])

    def test_cli(self):
        result = self.cli(self.input, "--output-dir", self.output)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("extracted 1 routes", result.stderr)
        target = self.output / "botroutes" / "mp" / "ffa1.demoroute"
        self.assertTrue(target.is_file())
        before = target.read_bytes()
        result = self.cli(self.input, "--output-dir", self.output)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(target.read_bytes(), before)
        result = self.cli(self.input, "--output-dir", self.output, "--force")
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.cli(self.directory / "missing.json", "--output-dir", self.output)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotEqual(self.cli(self.input).returncode, 0)
        self.assertNotEqual(self.cli("--unknown").returncode, 0)

    def test_cli_rejects_runtime_incompatible_paths_and_coordinates(self):
        for mutate in (lambda d: d.update(map="a" * 44),
                       lambda d: d["tracks"][0]["frames"][0].update(origin=[131073, 0, 0])):
            doc = document()
            mutate(doc)
            self.input.write_text(json.dumps(doc), encoding="utf-8")
            result = self.cli(self.input, "--output-dir", self.output)
            self.assertNotEqual(result.returncode, 0, result.stderr)
            self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
