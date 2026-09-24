"""Optional offline EDS import check using python-canopen (not a core dependency).

Usage: python verify_eds_third_party.py EXECUTABLE EDS [PACKAGE_DIRECTORY]
"""
import os
import subprocess
import sys

if len(sys.argv) == 4:
    sys.path.insert(0, os.path.abspath(sys.argv[3]))

import canopen
from canopen.objectdictionary import ODVariable


def verify(executable, eds_path):
    print(f"python-canopen {canopen.__version__}")
    for node_id in (1, 2, 127):
        od = canopen.import_od(eds_path, node_id)
        imported = {}
        for item in od.values():
            variables = [item] if isinstance(item, ODVariable) else item.values()
            for variable in variables:
                key = (variable.index, variable.subindex)
                imported[key] = (
                    variable.data_type,
                    max(1, (len(variable) + 7) // 8),
                    {"ro": 0, "rw": 1, "const": 2}[variable.access_type],
                    variable.default,
                    variable.min,
                    variable.max,
                )
        runtime = {}
        output = subprocess.check_output([executable, "dump", str(node_id)], text=True)
        for line in output.splitlines():
            fields = line.split(",")
            runtime[int(fields[0], 16), int(fields[1])] = tuple(map(int, fields[2:]))
        if len(imported) != 36 or imported != runtime:
            raise ValueError(f"Node {node_id}: imported EDS differs from C table")
        # Construct the third-party node offline; no network or CAN adapter is opened.
        canopen.RemoteNode(node_id, od)
        print(f"Node {node_id}: {len(od)} indices, {len(imported)} entries; "
              "all fields match C dump; RemoteNode created offline")


if __name__ == "__main__":
    verify(sys.argv[1], sys.argv[2])
