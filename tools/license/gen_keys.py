#!/usr/bin/env python3
"""Makes the Ed25519 key pair of the licence server (run ONCE, keep the private key secret).

  python3 tools/license/gen_keys.py [private_key_file]

 * The PRIVATE key is written to a file (default: license_private_key.txt, permissions 600) in the format the server wants:
   PKCS#8 DER, base64. Put it into the Supabase secret LICENSE_SIGNING_KEY, keep one offline backup, never commit it, never send it
   to anyone. Whoever holds it can make licences for every guitar. If it is lost, every player has to be given a new plugin build.
 * The PUBLIC key is printed: pass it to CMake as FOURREAL_LICENSE_PUBLIC_KEY (it is built into the plugin).
"""
import base64
import os
import sys

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey


def b64(b):
    return base64.b64encode(b).decode()


def b64u(b):
    return base64.urlsafe_b64encode(b).rstrip(b"=").decode()


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "license_private_key.txt"
    if os.path.exists(path):
        sys.exit("%s already exists. Refusing to overwrite a private key; choose another file name." % path)
    key = Ed25519PrivateKey.generate()
    der = key.private_bytes(serialization.Encoding.DER, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
    pub = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(b64(der) + "\n")
    print("Private key written to %s (secret, do not commit)." % path)
    print("Supabase secret:   supabase secrets set LICENSE_SIGNING_KEY=\"$(cat %s)\"" % path)
    print("Public key (for CMake FOURREAL_LICENSE_PUBLIC_KEY):")
    print(b64u(pub))


if __name__ == "__main__":
    main()
