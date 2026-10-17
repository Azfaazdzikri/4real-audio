"""Read a PyTorch .pt checkpoint WITHOUT torch (numpy only): enough for the plain state dicts this project saves.

    ck = load_pt("model.pt")   # dict; tensors become numpy arrays
"""
import collections
import io
import pickle
import zipfile

import numpy as np

_DTYPES = {"FloatStorage": np.float32, "HalfStorage": np.float16, "DoubleStorage": np.float64,
           "LongStorage": np.int64, "IntStorage": np.int32, "BoolStorage": np.bool_}


def _rebuild(storage, offset, size, stride, *rest):
    size, stride = tuple(size), tuple(stride)
    count = int(np.prod(size)) if size else 1
    flat = storage[offset:offset + (1 if not size else 1 + sum((s - 1) * st for s, st in zip(size, stride)))]
    out = np.lib.stride_tricks.as_strided(flat, shape=size, strides=[st * flat.itemsize for st in stride]) if size else flat[0]
    return np.array(out) if count else np.zeros(size)


def load_pt(path):
    z = zipfile.ZipFile(path)
    pkl = next(n for n in z.namelist() if n.endswith("data.pkl"))
    root = pkl[:-len("data.pkl")]

    class Unpickler(pickle.Unpickler):
        def find_class(self, module, name):
            if name == "_rebuild_tensor_v2":
                return _rebuild
            if module == "torch" and name.endswith("Storage"):
                return name
            if module == "collections":
                return getattr(collections, name)
            return super().find_class(module, name)

        def persistent_load(self, pid):
            _, storage, key, _loc, _n = pid
            storage = storage if isinstance(storage, str) else storage.__name__
            return np.frombuffer(z.read(f"{root}data/{key}"), dtype=_DTYPES[storage])

    return Unpickler(io.BytesIO(z.read(pkl))).load()
