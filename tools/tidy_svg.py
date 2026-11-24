#!/usr/bin/env python3
"""Tidy an UrsaSvgExporter SVG for editing in a graphics package.

The export mirrors JUCE's paint recursion, which is not the layer tree a
designer wants: every component sits inside its parent's clip group as well
as its own, a parent's own drawing lands in an unnamed group, components
without a componentID are named by class (Label_3), drop shadows are masked
fills, and dense graphs arrive as thousands of separate rects. This rewrites
the structure, keeping what renders:

- wrapper clip groups collapse into the component they wrap
- a clip is dropped when its content already lies inside it, so editing a
  label doesn't cut it off (text extents come from the exporter's data-bbox)
- class-named groups are prefixed by their owning control: mix_knob.Label
- unnamed groups become <owner>.paint (drawn before the children),
  <owner>.paintOver (after them) or <owner>.part
- masked fills (juce::DropShadow) become plain images named Shadow, since
  graphics packages read SVG masks inconsistently (needs Pillow; without it
  they stay masks)
- runs of same-coloured rects merge into one path per run
- gradients that borrow their stops through xlink:href get their own copy
- with --sections, consecutive top-level groups are wrapped in named
  sections; the JSON is [{"name": "top_bar", "ids": ["menu"]}, ...] with
  fnmatch patterns, and a section never reorders anything

Usage: tidy_svg.py [--sections FILE] [--out DIR] export.svg...
Without --out, each file is written beside its input as <name>.tidy.svg.
Background: the module README, "What you get".
"""
import argparse
import base64
import fnmatch
import io
import json
import re
import sys
from pathlib import Path
import xml.etree.ElementTree as ET

SVG = "http://www.w3.org/2000/svg"
XLINK = "http://www.w3.org/1999/xlink"
ET.register_namespace("", SVG)
ET.register_namespace("xlink", XLINK)
NUM = re.compile(r"-?\d*\.?\d+(?:[eE][-+]?\d+)?")
GENERATED = ("paint", "paintOver", "part", "Shadow")


def tag(e):
    return e.tag.split("}")[1]


class Box:
    """Axis-aligned bounds. None stands for empty, Box.UNKNOWN for content
    whose extent can't be read, which keeps any clip around it."""

    def __init__(self, x0, y0, x1, y1):
        self.x0, self.y0, self.x1, self.y1 = x0, y0, x1, y1

    @staticmethod
    def of_points(pts):
        if not pts:
            return None
        xs, ys = zip(*pts)
        return Box(min(xs), min(ys), max(xs), max(ys))

    def union(self, o):
        if o is None:
            return self
        return Box(min(self.x0, o.x0), min(self.y0, o.y0), max(self.x1, o.x1), max(self.y1, o.y1))

    def intersect(self, o):
        b = Box(max(self.x0, o.x0), max(self.y0, o.y0), min(self.x1, o.x1), min(self.y1, o.y1))
        return b if b.x0 <= b.x1 and b.y0 <= b.y1 else None

    def inside(self, o, tol=0.01):
        return (self.x0 >= o.x0 - tol and self.y0 >= o.y0 - tol
                and self.x1 <= o.x1 + tol and self.y1 <= o.y1 + tol)

    def as_path(self):
        return f"M {self.x0:g} {self.y0:g} L {self.x1:g} {self.y0:g} {self.x1:g} {self.y1:g} {self.x0:g} {self.y1:g} Z"


Box.UNKNOWN = Box(float("-inf"), float("-inf"), float("inf"), float("inf"))


class SvgTidier:
    def __init__(self, path, sections=()):
        self.tree = ET.parse(path)
        self.root = self.tree.getroot()
        self.defs = self.root.find(f"{{{SVG}}}defs")
        if self.defs is None:
            self.defs = ET.Element(f"{{{SVG}}}defs")
            self.root.insert(0, self.defs)
        self.clips = {cp.get("id"): cp for cp in self.defs.iter(f"{{{SVG}}}clipPath")}
        self.sections = sections
        self.next_clip = 0
        self.used_ids = set()
        self.section_ids = set()
        self.notes = []

    # --- clips --------------------------------------------------------------
    def clip_box(self, g):
        """The clip's bounds, and whether the clip is exactly that rectangle."""
        ref = g.get("clip-path")
        if not ref:
            return None, False
        shapes = list(self.clips[ref[5:-1]])
        if len(shapes) == 1 and tag(shapes[0]) == "path":
            nums = [float(n) for n in NUM.findall(shapes[0].get("d"))]
            pts = list(zip(nums[0::2], nums[1::2]))
            box = Box.of_points(pts)
            is_rect = len(pts) == 4 and all(
                x in (box.x0, box.x1) and y in (box.y0, box.y1) for x, y in pts)
            return box, is_rect
        box = None
        for s in shapes:
            box = self.bbox(s) if box is None else box.union(self.bbox(s))
        return box, False

    def set_rect_clip(self, g, box):
        cid = f"TidyClip{self.next_clip}"
        self.next_clip += 1
        cp = ET.SubElement(self.defs, f"{{{SVG}}}clipPath", {"id": cid})
        ET.SubElement(cp, f"{{{SVG}}}path", {"d": box.as_path()})
        self.clips[cid] = cp
        g.set("clip-path", f"url(#{cid})")

    # --- bounds -------------------------------------------------------------
    def bbox(self, e):
        """Conservative device bounds: a path's control points, not its curve."""
        t = tag(e)
        if t == "path":
            nums = [float(n) for n in NUM.findall(e.get("d", ""))]
            return Box.of_points(list(zip(nums[0::2], nums[1::2])))
        if t == "rect":
            x, y = float(e.get("x", 0)), float(e.get("y", 0))
            return Box(x, y, x + float(e.get("width")), y + float(e.get("height")))
        if t == "image":
            x, y = float(e.get("x", 0)), float(e.get("y", 0))
            w, h = float(e.get("width")), float(e.get("height"))
            a, b, c, d, tx, ty = 1, 0, 0, 1, 0, 0
            m = re.match(r"matrix\(([^)]*)\)", e.get("transform", ""))
            if m:
                a, b, c, d, tx, ty = (float(v) for v in re.split(r"[ ,]+", m.group(1).strip()))
            elif e.get("transform"):
                return Box.UNKNOWN
            return Box.of_points([(a * px + c * py + tx, b * px + d * py + ty)
                                  for px, py in ((x, y), (x + w, y), (x, y + h), (x + w, y + h))])
        if t == "text":
            nums = [float(n) for n in NUM.findall(e.get("data-bbox", ""))]
            return Box(*nums) if len(nums) == 4 else Box.UNKNOWN
        if t == "g":
            if e.get("transform"):
                return Box.UNKNOWN
            box = None
            for k in e:
                b = self.bbox(k)
                if b is not None:
                    box = b if box is None else box.union(b)
            clip, _ = self.clip_box(e)
            if clip is not None and box is not None:
                box = box.intersect(clip)
            return box
        if t in ("line", "polygon", "polyline", "circle", "ellipse", "use"):
            return Box.UNKNOWN
        return None

    # --- passes -------------------------------------------------------------
    def bake_masks(self):
        """Each juce::DropShadow arrives as a rect filled through an alpha
        mask. Bake it into a black image whose alpha is the rect's opacity
        times the mask's coverage (luminance x alpha, as SVG defines masks)."""
        try:
            from PIL import Image
        except ImportError:
            if any(g.get("mask") for g in self.root.iter(f"{{{SVG}}}g")):
                self.notes.append("Pillow not installed: masked shadows left as masks")
            return
        masks = {m.get("id"): m for m in self.defs.iter(f"{{{SVG}}}mask")}
        for g in list(self.root.iter(f"{{{SVG}}}g")):
            inner = list(g)
            ref = g.get("mask")
            if not ref or len(inner) != 1 or tag(inner[0]) != "rect" or ref[5:-1] not in masks:
                continue
            mask, rect = masks[ref[5:-1]], inner[0]
            if len(mask) != 1 or tag(mask[0]) != "image" or rect.get("fill", "").startswith("url("):
                continue
            src = mask[0]
            png = Image.open(io.BytesIO(base64.b64decode(
                src.get(f"{{{XLINK}}}href").split(",", 1)[1]))).convert("RGBA")
            opacity = float(rect.get("fill-opacity", "1"))
            lum = png.convert("L").tobytes()
            alpha = png.getchannel("A").tobytes()
            coverage = Image.frombytes("L", png.size, bytes(
                round(l * a / 255 * opacity) for l, a in zip(lum, alpha)))
            colour = self.parse_rgb(rect.get("fill", "rgb(0,0,0)"))
            out = Image.new("RGBA", png.size, colour + (0,))
            out.putalpha(coverage)
            buf = io.BytesIO()
            out.save(buf, "PNG")
            image = ET.Element(f"{{{SVG}}}image",
                               {k: v for k, v in src.attrib.items() if k != f"{{{XLINK}}}href"})
            image.set(f"{{{XLINK}}}href", "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode())
            del g.attrib["mask"]
            g.remove(rect)
            g.append(image)
            g.set("id", g.get("id") or "Shadow")

    @staticmethod
    def parse_rgb(fill):
        m = re.match(r"rgb\((\d+),\s*(\d+),\s*(\d+)\)", fill)
        return tuple(int(v) for v in m.groups()) if m else (0, 0, 0)

    def collapse_wrappers(self, e):
        """An unnamed group holding only another group, with nothing but a
        clip of its own, merges into that group (clips intersect)."""
        for k in list(e):
            if tag(k) == "g":
                self.collapse_wrappers(k)
        for i, k in enumerate(list(e)):
            if tag(k) != "g" or k.get("id") or set(k.attrib) - {"clip-path"}:
                continue
            inner = list(k)
            if len(inner) != 1 or tag(inner[0]) != "g" or inner[0].get("transform"):
                continue
            child = inner[0]
            outer_box, outer_rect = self.clip_box(k)
            inner_box, inner_rect = self.clip_box(child)
            if outer_box is not None:
                if inner_box is None:
                    child.set("clip-path", k.get("clip-path"))
                elif not inner_box.inside(outer_box):
                    both = outer_box.intersect(inner_box)
                    if not (outer_rect and inner_rect) or both is None:
                        continue  # the intersection isn't one rectangle
                    self.set_rect_clip(child, both)
            e.remove(k)
            e.insert(i, child)

    def drop_redundant_clips(self, e):
        for k in e:
            if tag(k) == "g":
                self.drop_redundant_clips(k)
        if tag(e) != "g" or not e.get("clip-path"):
            return
        clip, _ = self.clip_box(e)
        ref = e.attrib.pop("clip-path")
        content = self.bbox(e)
        # content touching the clip edge keeps it: the clip trims its
        # anti-aliased fringe, which would otherwise show
        if content is not None and not content.inside(clip, tol=-1.0):
            e.set("clip-path", ref)

    def merge_rect_runs(self, e, min_rects=100):
        for k in e:
            if tag(k) == "g":
                self.merge_rect_runs(k, min_rects)
        kids = list(e)
        if sum(tag(k) == "rect" for k in kids) < min_rects:
            return
        plain = {"x", "y", "width", "height", "fill", "fill-opacity"}

        def key(k):
            if tag(k) != "rect" or set(k.attrib) - plain:
                return None
            return (k.get("fill"), k.get("fill-opacity", "1"))

        out, run = [], []

        def flush():
            if len(run) > 1 and self.run_mergeable(run):
                out.append(ET.Element(f"{{{SVG}}}path", {
                    "d": " ".join(self.bbox(r).as_path() for r in run),
                    "fill": run[0].get("fill"),
                    "fill-opacity": run[0].get("fill-opacity", "1")}))
            else:
                out.extend(run)
            run.clear()

        for k in kids:
            if run and key(k) != key(run[0]):
                flush()
            if key(k) is None:
                out.append(k)
            else:
                run.append(k)
        flush()
        for k in kids:
            e.remove(k)
        e.extend(out)

    def run_mergeable(self, run):
        """Merged rects fill as a union; separate translucent rects darken
        where they overlap, so only merge translucent runs that don't."""
        if float(run[0].get("fill-opacity", "1")) >= 1:
            return True
        boxes = sorted((self.bbox(r) for r in run), key=lambda b: b.x0)
        for i, a in enumerate(boxes):
            for b in boxes[i + 1:]:
                if b.x0 >= a.x1:
                    break
                if b.y0 < a.y1 and a.y0 < b.y1:
                    return False
        return True

    def inline_gradient_stops(self):
        """The exporter dedupes gradients: one with the same stops but other
        coordinates borrows its stops through xlink:href, which not every
        graphics package follows. Copy the stops in and drop the link."""
        href = f"{{{XLINK}}}href"
        grads = {g.get("id"): g for g in self.defs if tag(g) in ("linearGradient", "radialGradient")}

        def stops_of(g, seen=()):
            own = [k for k in g if tag(k) == "stop"]
            ref = (g.get(href) or "")[1:]
            if own or not ref or ref in seen or ref not in grads:
                return own
            return stops_of(grads[ref], seen + (ref,))

        for g in grads.values():
            if g.get(href):
                if not any(tag(k) == "stop" for k in g):
                    for st in stops_of(g):
                        g.append(ET.fromstring(ET.tostring(st)))
                del g.attrib[href]

    # --- naming -------------------------------------------------------------
    @staticmethod
    def is_class_name(i):
        """The exporter names a component by class when it has no
        componentID; componentIDs are lower_snake_case."""
        return bool(re.match(r"^[A-Z][A-Za-z0-9]*(_\d+)?$", i))

    def unique(self, name):
        n, i = name, 2
        while n in self.used_ids:
            n, i = f"{name}_{i}", i + 1
        self.used_ids.add(n)
        return n

    def short(self, owner):
        """The control part of an owner id: er_controls, not
        secondary_controls.er_controls; generated names stay whole."""
        last = owner.split(".")[-1]
        if last in GENERATED or self.is_class_name(last):
            return owner
        return last

    def rename(self, e, owner):
        """owner: the id that unnamed and class-named groups below are named
        after (the nearest control, or a top-level class-named group)."""
        kids = list(e)
        named = [i for i, k in enumerate(kids) if tag(k) == "g" and k.get("id")]
        for i, k in enumerate(kids):
            if tag(k) != "g":
                continue
            kid_id = k.get("id")
            if e is self.root and kid_id in self.section_ids:
                self.rename(k, None)
                continue
            if kid_id is None:
                if owner is None:
                    continue
                if named and i < named[0]:
                    kid_id = f"{self.short(owner)}.paint"
                elif named and i > named[-1]:
                    kid_id = f"{self.short(owner)}.paintOver"
                else:
                    kid_id = f"{self.short(owner)}.part"
                child_owner = kid_id
            elif self.is_class_name(kid_id) and owner:
                kid_id = f"{self.short(owner)}.{re.sub(r'_[0-9]+$', '', kid_id)}"
                child_owner = owner
            else:
                child_owner = kid_id
            kid_id = self.unique(kid_id)
            k.set("id", kid_id)
            self.rename(k, child_owner)

    # --- top level ----------------------------------------------------------
    def section_of(self, e):
        i = e.get("id") or ""
        for s in self.sections:
            if any(fnmatch.fnmatchcase(i, pattern) for pattern in s["ids"]):
                return s["name"]
        return None

    def group_sections(self):
        kids = [k for k in self.root if k is not self.defs]
        if kids and tag(kids[0]) == "g" and not kids[0].get("id"):
            kids[0].set("id", "background")
        if not self.sections:
            return
        for k in kids:
            self.root.remove(k)
        current, current_name = None, None
        for k in kids:
            name = self.section_of(k)
            if name is None:
                self.root.append(k)
                current, current_name = None, None
                continue
            if name != current_name:
                sid = self.unique(name)
                self.section_ids.add(sid)
                current = ET.SubElement(self.root, f"{{{SVG}}}g", {"id": sid})
                current_name = name
            current.append(k)

    def drop_unused_defs(self):
        for _ in range(2):  # a def used only by a removed def goes second time round
            text = ET.tostring(self.root, encoding="unicode")
            for d in list(self.defs):
                i = d.get("id")
                if i and f"#{i})" not in text and f'#{i}"' not in text:
                    self.defs.remove(d)

    def run(self, out_path):
        self.bake_masks()
        self.collapse_wrappers(self.root)
        self.drop_redundant_clips(self.root)
        self.merge_rect_runs(self.root)
        self.group_sections()
        self.rename(self.root, None)
        self.inline_gradient_stops()
        self.drop_unused_defs()
        ET.indent(self.tree, space=" ")
        self.tree.write(out_path, encoding="UTF-8", xml_declaration=True)
        return self.notes


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--sections", type=Path, help="JSON list of {name, ids} top-level sections")
    parser.add_argument("--out", type=Path, help="output folder (default: beside each input, as .tidy.svg)")
    args = parser.parse_args()

    sections = json.loads(args.sections.read_text()) if args.sections else ()
    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
    for f in args.files:
        out = args.out / f.name if args.out else f.with_suffix(".tidy.svg")
        notes = SvgTidier(f, sections).run(out)
        print(f"{f.name} -> {out}" + "".join(f"\n  {n}" for n in notes))


if __name__ == "__main__":
    sys.exit(main())
