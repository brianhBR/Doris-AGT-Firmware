"""Post-build script: copies firmware.bin to build/ with a descriptive name."""

import os
import shutil

Import("env")

def copy_firmware(source, target, env):
    src = os.path.join(env.subst("$BUILD_DIR"), "firmware.bin")
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
    dst = os.path.join(out_dir, f"{base}.bin")

    shutil.copy2(src, dst)
    print(f"  Firmware copied -> build/{base}.bin")

env.AddPostAction("$BUILD_DIR/firmware.bin", copy_firmware)
