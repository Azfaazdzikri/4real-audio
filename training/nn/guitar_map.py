"""Mapping of training cell names to the plugin's guitar codes and pickup positions. numpy/torch free."""

FORMAT_NOTE = "shared by export_ngm.py (v2) and export_spec.py (v3)"

# Plugin guitar codes in the order the plugin lists them, with the selector positions that exist in the data.
GUITARS = [
    ("LP", ["bridge", "middle", "neck"]),
    ("335", ["neck"]),
    ("SG", ["bridge", "neck"]),
    ("PRS", ["bridge", "middle", "neck"]),
    ("RIC", ["bridge"]),
    ("STR", ["bridge", "bridge-middle", "middle-neck", "neck"]),
    ("TCR", ["bridge", "neck"]),
    ("FLC", ["middle"]),
]


def guitar_of(cell: str):
    s = cell.lower()
    if "g6136" in s or "falcon" in s:
        return "FLC"
    if "335" in s:
        return "335"
    if "gibson sg" in s or " sg " in s:
        return "SG"
    if "prs" in s:
        return "PRS"
    if "rickenbacker" in s:
        return "RIC"
    if "strato" in s:
        return "STR"
    if "telecaster" in s:
        return "TCR"
    if "lespaul" in s or "les paul" in s:
        return "LP"
    raise ValueError(f"unknown guitar in cell name: {cell}")


def position_of(cell: str):
    p = cell.split("|", 1)[1].lower()
    if "bridge-middle" in p:
        return "bridge-middle"
    if "middle-neck" in p:
        return "middle-neck"
    if "bridge" in p:
        return "bridge"
    if "neck" in p:
        return "neck"
    if "middle" in p:
        return "middle"
    raise ValueError(f"unknown position in cell name: {cell}")
