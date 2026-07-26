Import("env")
import os

def load_env_file(path):
    values = {}
    if not os.path.isfile(path):
        print(f"[load_env] WARNING: {path} not found -- WIFI_SSID/WIFI_PASSWORD will be undefined")
        return values
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            values[key.strip()] = value.strip()
    return values

def c_string_escape(value):
    # Escape backslashes and double quotes so the value is safe inside a C string literal.
    # Spaces need no escaping at all here -- that's the whole point of this approach.
    return value.replace("\\", "\\\\").replace('"', '\\"')

env_vars = load_env_file(os.path.join(env.get("PROJECT_DIR"), ".env"))

header_path = os.path.join(env.get("PROJECT_DIR"), "include", "wifi_secrets.h")
os.makedirs(os.path.dirname(header_path), exist_ok=True)

with open(header_path, "w") as f:
    f.write("#pragma once\n")
    f.write(f'#define WIFI_SSID "{c_string_escape(env_vars.get("WIFI_SSID", ""))}"\n')
    f.write(f'#define WIFI_PASSWORD "{c_string_escape(env_vars.get("WIFI_PASSWORD", ""))}"\n')

print("[load_env] Generated include/wifi_secrets.h from .env")
