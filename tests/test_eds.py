"""EDS 静态结构与真实 C 对象表一致性检查；仅使用 Python 标准库。"""
import configparser
import re
import subprocess
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def number(value, node):
    if value.startswith("$NODEID+"):
        return node + int(value[len("$NODEID+"):], 0)
    return int(value, 0)


def verify(executable, path):
    eds = configparser.ConfigParser(interpolation=None, strict=True)
    with open(path, encoding="ascii") as stream:
        eds.read_file(stream)
    require(eds["DeviceInfo"]["Granularity"] == "0", "fixed mapping requires Granularity=0")
    require(eds["FileInfo"]["EDSVersion"] == "4.0", "EDS format version")
    sections = {}
    for name in eds.sections():
        match = re.fullmatch(r"([0-9A-Fa-f]{4})(?:sub([0-9A-Fa-f]+))?", name)
        if match:
            sections[(int(match[1], 16), None if match[2] is None else int(match[2], 16))] = eds[name]
    declared = []
    for category in ("MandatoryObjects", "OptionalObjects", "ManufacturerObjects"):
        section = eds[category]
        count = int(section["SupportedObjects"])
        require(len(section) == count + 1, f"{category}: count mismatch")
        declared += [int(section[str(i)], 0) for i in range(1, count + 1)]
    require(len(declared) == len(set(declared)), "duplicate index in object lists")
    require(set(declared) == {idx for idx, sub in sections if sub is None}, "object lists mismatch")
    for (idx, sub), section in sections.items():
        if sub is None and int(section["ObjectType"]) in (8, 9):
            children = [s for i, s in sections if i == idx and s is not None]
            require(len(children) == int(section["SubNumber"]), f"{idx:X}: SubNumber")
            require(0 in children, f"{idx:X}: missing sub0")
    lengths = {1: 1, 3: 2, 5: 1, 6: 2, 7: 4}
    for node in (1, 2, 127):
        output = subprocess.check_output([executable, "dump", str(node)], text=True)
        runtime = {}
        for line in output.splitlines():
            fields = line.split(",")
            key = (int(fields[0], 16), int(fields[1]))
            require(key not in runtime, f"duplicate C entry {key}")
            runtime[key] = tuple(map(int, fields[2:]))
        exported = {}
        for (idx, sub), section in sections.items():
            if int(section["ObjectType"]) != 7:
                continue
            key = (idx, 0 if sub is None else sub)
            typ = int(section["DataType"], 0)
            exported[key] = (typ, lengths[typ], {"ro": 0, "rw": 1, "const": 2}[section["AccessType"]],
                             number(section["DefaultValue"], node),
                             int(section["LowLimit"], 0), int(section["HighLimit"], 0))
        require(runtime == exported, f"node {node}: EDS differs from initialized C table")
        require(len(runtime) == 36, "unexpected entry count")
        # 独立核对固定 PDO 的方向、映射长度和可映射标志。
        expected_maps = {0x1600: [0x62000108], 0x1A00: [0x60000108],
                         0x1A01: [0x64010110, 0x64010210]}
        for index, maps in expected_maps.items():
            require(exported[index, 0][3] == len(maps), "mapping count")
            for sub, mapping in enumerate(maps, 1):
                require(exported[index, sub][3] == mapping, "mapping value")
                target = (mapping >> 16, (mapping >> 8) & 255)
                require(exported[target][1] * 8 == mapping & 255, "mapped bit length")
                require(sections[target]["PDOMapping"] == "1", "unmappable target")
                if index == 0x1600:
                    require(exported[target][2] == 1, "RPDO target must be writable")
            require(sum(m & 255 for m in maps) <= 64, "PDO exceeds 8 bytes")
        for key, section in sections.items():
            if int(section["ObjectType"]) == 7:
                eligible = key in {(0x6000, 1), (0x6200, 1), (0x6401, 1), (0x6401, 2)}
                require(int(section["PDOMapping"]) == int(eligible), "unexpected PDO eligibility")
        for index in (0x1800, 0x1801):
            require((index, 4) not in exported, "reserved sub4 exposed")
            require(exported[index, 0][3] == 5, "highest subindex must be 5")
            require(exported[index, 1][3] & 0x40000000, "TPDO must prohibit RTR")
        require(exported[0x6423, 0] == (1, 1, 1, 0, 0, 1), "analog interrupt Boolean default")
        require(exported[0x1000, 0][3] == 0x70191, "DI/DO/AI profile flags")
        for index in (0x1800, 0x1801):
            require(exported[index, 3][2:4] == (2, 0), "inhibit must be fixed zero")
            require(exported[index, 5][3] == 0, "default event timer")
        for sub in (1, 2):
            require(exported[0x6401, sub][0] == 3 and exported[0x6401, sub][5] == 32760,
                    "positive left-adjusted AI range")
        for field, sub in (("VendorNumber", 1), ("ProductNumber", 2), ("RevisionNumber", 3)):
            require(int(eds["DeviceInfo"][field], 0) == exported[0x1018, sub][3], "identity mismatch")
    print("EDS structure and 36 C entries match at Node-ID 1, 2 and 127.")


if __name__ == "__main__":
    verify(sys.argv[1], sys.argv[2])
