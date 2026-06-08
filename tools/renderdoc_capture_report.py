#!/usr/bin/env python3
"""
renderdoc_capture_report.py — offline RenderDoc capture analyser for N-Ray.

Usage:
    python3 tools/renderdoc_capture_report.py capture.rdc [--json-out report.json]

Requires the RenderDoc Python module (renderdoc / _renderdoc).
If unavailable, prints setup guidance and exits non-zero.
"""

import sys
import json
import argparse

# ---------------------------------------------------------------------------
# RenderDoc module import — hard requirement, not bundled.
# ---------------------------------------------------------------------------

try:
    if 'renderdoc' not in sys.modules and '_renderdoc' not in sys.modules:
        import renderdoc
    import renderdoc as rd
except ImportError:
    print(
        "ERROR: RenderDoc Python module not found.\n"
        "\n"
        "To use this script the 'renderdoc' Python module must be installed and\n"
        "must match the Python version you are running.  Options:\n"
        "\n"
        "  1. Run the script from inside qrenderdoc's embedded Python console\n"
        "     (the module is always available there).\n"
        "\n"
        "  2. Install the system RenderDoc package that ships Python bindings,\n"
        "     then run with the matching Python interpreter.  Example on Ubuntu:\n"
        "       sudo apt install renderdoc\n"
        "       python3 tools/renderdoc_capture_report.py capture.rdc\n"
        "\n"
        "  3. Build RenderDoc from source with -DENABLE_PYTHON_VERSION=3 and\n"
        "     add the resulting .so directory to PYTHONPATH.\n"
        "\n"
        "The capture .rdc file itself is always valid for manual inspection with\n"
        "qrenderdoc regardless of whether this script can run.",
        file=sys.stderr,
    )
    sys.exit(1)

# ---------------------------------------------------------------------------
# N-Ray binding names (must match descriptor contract in vulkan_compute_preview.cpp)
# ---------------------------------------------------------------------------

NRAY_BINDING_NAMES = {
    0:  "pixel readback",
    1:  "triangle intersection SSBO",
    2:  "triangle shading SSBO",
    3:  "material SSBO",
    4:  "BVH SSBO",
    5:  "HDR accumulation buffer",
    6:  "render settings",
    7:  "glTF textures",
    8:  "shadow map",
    9:  "resolved HDR (storage image)",
    10: "normal/roughness (storage image)",
    11: "albedo/metallic (storage image)",
    12: "depth (storage image)",
    13: "material id (storage image)",
    14: "instance id (storage image)",
    15: "denoiser ping (storage image)",
    16: "denoiser pong (storage image)",
}

# ---------------------------------------------------------------------------
# Action tree walker
# ---------------------------------------------------------------------------

def action_to_dict(controller, action, depth=0):
    name = action.GetName(controller.GetStructuredFile())
    entry = {
        "eventId":  action.eventId,
        "name":     name,
        "depth":    depth,
        "flags":    int(action.flags),
        "children": [],
    }
    # Record dispatch dimensions when available.
    if action.flags & rd.ActionFlags.Dispatch:
        entry["dispatchDimension"] = list(action.dispatchDimension)
    for child in action.children:
        entry["children"].append(action_to_dict(controller, child, depth + 1))
    return entry


def collect_compute_dispatches(action_dict, result=None):
    if result is None:
        result = []
    if action_dict["flags"] & int(rd.ActionFlags.Dispatch):
        result.append(action_dict)
    for child in action_dict["children"]:
        collect_compute_dispatches(child, result)
    return result


def print_action_tree(action_dict, indent=""):
    print(f"{indent}{action_dict['eventId']}: {action_dict['name']}")
    for child in action_dict["children"]:
        print_action_tree(child, indent + "    ")

# ---------------------------------------------------------------------------
# Descriptor access report
# ---------------------------------------------------------------------------

def report_descriptors(controller, event_id):
    controller.SetFrameEvent(event_id, True)
    try:
        accesses = controller.GetDescriptorAccess(event_id)
    except AttributeError:
        return []

    entries = []
    for acc in accesses:
        binding = getattr(acc, "index", None)
        binding_name = NRAY_BINDING_NAMES.get(binding, f"binding {binding}")
        entries.append({
            "binding":     binding,
            "bindingName": binding_name,
        })
    return entries

# ---------------------------------------------------------------------------
# Capture loader
# ---------------------------------------------------------------------------

def load_capture(filename):
    cap = rd.OpenCaptureFile()
    result = cap.OpenFile(filename, '', None)
    if result != rd.ResultCode.Succeeded:
        raise RuntimeError(f"OpenFile failed: {result}")
    if not cap.LocalReplaySupport():
        raise RuntimeError("Capture cannot be replayed on this machine")
    result, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    if result != rd.ResultCode.Succeeded:
        raise RuntimeError(f"OpenCapture failed: {result}")
    return cap, controller

# ---------------------------------------------------------------------------
# Main analysis
# ---------------------------------------------------------------------------

def run(controller, verbose=False):
    roots = controller.GetRootActions()
    action_trees = [action_to_dict(controller, a) for a in roots]

    if verbose:
        print("\n=== Action Tree ===")
        for tree in action_trees:
            print_action_tree(tree)

    dispatches = []
    for tree in action_trees:
        dispatches.extend(collect_compute_dispatches(tree))

    print(f"\nFound {len(dispatches)} compute dispatch(es):")
    for d in dispatches:
        dims = d.get("dispatchDimension", "?")
        print(f"  [{d['eventId']}] {d['name']}  dims={dims}")

    # Descriptor access for the first dispatch.
    descriptor_report = {}
    if dispatches:
        first_eid = dispatches[0]["eventId"]
        print(f"\nDescriptor access at eventId {first_eid} ({dispatches[0]['name']}):")
        descs = report_descriptors(controller, first_eid)
        for e in descs:
            print(f"  binding {e['binding']:>2}: {e['bindingName']}")
        descriptor_report[str(first_eid)] = descs

    return {
        "actionTree":   action_trees,
        "dispatches":   dispatches,
        "descriptors":  descriptor_report,
    }

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Analyse an N-Ray RenderDoc capture and summarise Vulkan compute state."
    )
    parser.add_argument("capture", help="Path to .rdc capture file")
    parser.add_argument("--json-out", metavar="PATH", help="Write JSON report to this file")
    parser.add_argument("--verbose", action="store_true", help="Print full action tree")
    args = parser.parse_args()

    print(f"Loading capture: {args.capture}")
    rd.InitialiseReplay(rd.GlobalEnvironment(), [])

    cap = controller = None
    try:
        cap, controller = load_capture(args.capture)
        report = run(controller, verbose=args.verbose)

        if args.json_out:
            with open(args.json_out, "w") as f:
                json.dump(report, f, indent=2, default=str)
            print(f"\nJSON report written: {args.json_out}")

    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        if controller is not None:
            controller.Shutdown()
        if cap is not None:
            cap.Shutdown()
        rd.ShutdownReplay()


if __name__ == "__main__":
    main()
