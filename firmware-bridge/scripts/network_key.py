# PlatformIO pre-build script: generate danfoss_key.h from the macOS Keychain
# (service danfoss-link-network-key) or DANFOSS_NETWORK_KEY, inside the build
# directory only. The key never lives in src/ or in git. Prints no key material.
import os, re, subprocess
Import("env")

SERVICE = "danfoss-link-network-key"
value = os.environ.get("DANFOSS_NETWORK_KEY", "").strip()
if not value:
    r = subprocess.run(["security", "find-generic-password", "-s", SERVICE, "-w"],
                       capture_output=True, text=True)
    value = r.stdout.strip() if r.returncode == 0 else ""
if not re.fullmatch(r"[0-9a-fA-F]{32}", value):
    env.Exit("network key unavailable: add Keychain item '%s' or set DANFOSS_NETWORK_KEY "
             "(see docs/OPERATIONS.md, Network key)" % SERVICE)

out_dir = os.path.join(env.subst("$BUILD_DIR"), "generated")
os.makedirs(out_dir, exist_ok=True)
header = os.path.join(out_dir, "danfoss_key.h")
body = ("#pragma once\n// Generated at build time from the Keychain. Do not copy or commit.\n"
        "static const uint8_t danfossKey[16] = {%s};\n"
        % ",".join("0x%s" % value[i:i + 2].lower() for i in range(0, 32, 2)))
if not os.path.exists(header) or open(header).read() != body:
    fd = os.open(header, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(body)
env.Prepend(CPPPATH=[out_dir])
print("network key: generated build header from %s" %
      ("DANFOSS_NETWORK_KEY" if os.environ.get("DANFOSS_NETWORK_KEY") else "Keychain"))
