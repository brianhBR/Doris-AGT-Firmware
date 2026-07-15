"""Post-build script: copies firmware.bin to build/ with a descriptive name."""

import os
import shutil

Import("env")

def copy_firmware(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    src = os.path.join(build_dir, "firmware.bin")
    if not os.path.exists(src):
        return

    out_dir = os.path.join(env.subst("$PROJECT_DIR"), "build")
    os.makedirs(out_dir, exist_ok=True)

    env_name = env.subst("$PIOENV")
    name_map = {
        "SparkFun_RedBoard_Artemis_ATP": "doris-agt",
        "no-relays": "doris-agt-no-relays",
        "selftest": "doris-agt-selftest",
    }
    base = name_map.get(env_name, f"doris-agt-{env_name}")

    shutil.copy2(src, os.path.join(out_dir, f"{base}.bin"))
    print(f"  Firmware copied -> build/{base}.bin")

    # Also copy the linked ELF (named "program" on this Apollo3 platform) so
    # symbol/debug artifacts land in build/ with a consistent, labelled name.
    elf_src = os.path.join(build_dir, "program")
    if os.path.exists(elf_src):
        shutil.copy2(elf_src, os.path.join(out_dir, f"{base}.elf"))
        print(f"  ELF copied      -> build/{base}.elf")

env.AddPostAction("$BUILD_DIR/firmware.bin", copy_firmware)
