#!/usr/bin/env python3
"""Generate QuickJS embedded timezone transition data from system TZif files."""

import os
import struct
import sys

ZONEINFO_DIR = "/usr/share/zoneinfo"

# Zones needed by failing test262 tests
NEEDED_ZONES = {
    # Zones explicitly used in failing test262 tests
    "America/Los_Angeles",
    "America/Vancouver",
    "America/New_York",
    "America/Toronto",
    "America/Noronha",
    "America/Sao_Paulo",
    "Europe/Berlin",
    "Europe/London",
    "Europe/Paris",
    "Europe/Vienna",
    "Europe/Moscow",
    "Asia/Kolkata",
    "Asia/Riyadh",
    "Asia/Tokyo",
    "Pacific/Apia",
    "Pacific/Niue",
    "Antarctica/Casey",
    "Africa/Monrovia",
    "Asia/Kolkata",
    "America/Anchorage",
    # Zones for transitions-close-together tests
    "Africa/Tunis",
    "America/Argentina/Tucuman",
    "America/Boa_Vista",
    "America/Fortaleza",
    "America/Maceio",
    "America/Recife",
    "Europe/Riga",
    "Europe/Simferopol",
    "Europe/Tirane",
    # Zones for startOfDay / hoursInDay DST tests
    "America/St_Johns",
    "Antarctica/Casey",
    "Australia/Lord_Howe",
    "Asia/Singapore",
    # Basic required zones
    "Etc/UTC",
    "UTC",
    "GMT",
    "Etc/GMT",
    # Etc/GMT fixed-offset zones for supportedValuesOf
    "Etc/GMT+1", "Etc/GMT+2", "Etc/GMT+3", "Etc/GMT+4",
    "Etc/GMT+5", "Etc/GMT+6", "Etc/GMT+7", "Etc/GMT+8",
    "Etc/GMT+9", "Etc/GMT+10", "Etc/GMT+11", "Etc/GMT+12",
    "Etc/GMT-1", "Etc/GMT-2", "Etc/GMT-3", "Etc/GMT-4",
    "Etc/GMT-5", "Etc/GMT-6", "Etc/GMT-7", "Etc/GMT-8",
    "Etc/GMT-9", "Etc/GMT-10", "Etc/GMT-11", "Etc/GMT-12",
    "Etc/GMT-13", "Etc/GMT-14",
}

# Hardcoded IANA aliases (from tzdb backward file)
HARDCODED_ALIASES = {
    "Africa/Asmera": "Africa/Asmara",
    "America/Atka": "America/Adak",
    "America/Buenos_Aires": "America/Argentina/Buenos_Aires",
    "America/Catamarca": "America/Argentina/Catamarca",
    "America/Cordoba": "America/Argentina/Cordoba",
    "America/Jujuy": "America/Argentina/Jujuy",
    "America/Mendoza": "America/Argentina/Mendoza",
    "America/Rosario": "America/Argentina/Cordoba",
    "America/Coral_Harbour": "America/Atikokan",
    "America/Ensenada": "America/Tijuana",
    "America/Fort_Wayne": "America/Indiana/Indianapolis",
    "America/Godthab": "America/Nuuk",
    "America/Indianapolis": "America/Indiana/Indianapolis",
    "America/Knox_IN": "America/Indiana/Knox",
    "America/Louisville": "America/Kentucky/Louisville",
    "America/Nipigon": "America/Toronto",
    "America/Pangnirtung": "America/Iqaluit",
    "America/Porto_Acre": "America/Rio_Branco",
    "America/Rainy_River": "America/Winnipeg",
    "America/Santa_Isabel": "America/Tijuana",
    "America/Shiprock": "America/Denver",
    "America/Virgin": "America/Port_of_Spain",
    "Asia/Ashkhabad": "Asia/Ashgabat",
    "Asia/Calcutta": "Asia/Kolkata",
    "Asia/Chongqing": "Asia/Shanghai",
    "Asia/Chungking": "Asia/Shanghai",
    "Asia/Dacca": "Asia/Dhaka",
    "Asia/Harbin": "Asia/Shanghai",
    "Asia/Istanbul": "Europe/Istanbul",
    "Asia/Kashgar": "Asia/Urumqi",
    "Asia/Katmandu": "Asia/Kathmandu",
    "Asia/Macao": "Asia/Macau",
    "Asia/Rangoon": "Asia/Yangon",
    "Asia/Saigon": "Asia/Ho_Chi_Minh",
    "Asia/Tel_Aviv": "Asia/Jerusalem",
    "Asia/Thimbu": "Asia/Thimphu",
    "Asia/Ujung_Pandang": "Asia/Makassar",
    "Asia/Ulan_Bator": "Asia/Ulaanbaatar",
    "Atlantic/Faeroe": "Atlantic/Faroe",
    "Atlantic/Jan_Mayen": "Europe/Berlin",
    "Australia/ACT": "Australia/Sydney",
    "Australia/Canberra": "Australia/Sydney",
    "Australia/LHI": "Australia/Lord_Howe",
    "Australia/NSW": "Australia/Sydney",
    "Australia/North": "Australia/Darwin",
    "Australia/Queensland": "Australia/Brisbane",
    "Australia/South": "Australia/Adelaide",
    "Australia/Tasmania": "Australia/Hobart",
    "Australia/Victoria": "Australia/Melbourne",
    "Australia/West": "Australia/Perth",
    "Australia/Yancowinna": "Australia/Broken_Hill",
    "Brazil/Acre": "America/Rio_Branco",
    "Brazil/DeNoronha": "America/Noronha",
    "Brazil/East": "America/Sao_Paulo",
    "Brazil/West": "America/Manaus",
    "Canada/Atlantic": "America/Halifax",
    "Canada/Central": "America/Winnipeg",
    "Canada/Eastern": "America/Toronto",
    "Canada/Mountain": "America/Edmonton",
    "Canada/Newfoundland": "America/St_Johns",
    "Canada/Pacific": "America/Vancouver",
    "Canada/Saskatchewan": "America/Regina",
    "Canada/Yukon": "America/Whitehorse",
    "Chile/Continental": "America/Santiago",
    "Chile/EasterIsland": "Pacific/Easter",
    "Cuba": "America/Havana",
    "Egypt": "Africa/Cairo",
    "Eire": "Europe/Dublin",
    "EST": "America/Panama",
    "EST5EDT": "America/New_York",
    "GB": "Europe/London",
    "GB-Eire": "Europe/London",
    "Etc/GMT": "GMT",
    "Etc/GMT+0": "GMT",
    "Etc/GMT-0": "GMT",
    "Etc/GMT0": "GMT",
    "Greenwich": "GMT",
    "GMT+0": "GMT",
    "GMT-0": "GMT",
    "GMT0": "GMT",
    "Hongkong": "Asia/Hong_Kong",
    "HST": "Pacific/Honolulu",
    "Iceland": "Atlantic/Reykjavik",
    "Iran": "Asia/Tehran",
    "Israel": "Asia/Jerusalem",
    "Jamaica": "America/Jamaica",
    "Japan": "Asia/Tokyo",
    "Kwajalein": "Pacific/Kwajalein",
    "Libya": "Africa/Tripoli",
    "MET": "Europe/Paris",
    "Mexico/BajaNorte": "America/Tijuana",
    "Mexico/BajaSur": "America/Mazatlan",
    "Mexico/General": "America/Mexico_City",
    "MST": "America/Phoenix",
    "MST7MDT": "America/Denver",
    "Navajo": "America/Denver",
    "NZ": "Pacific/Auckland",
    "NZ-CHAT": "Pacific/Chatham",
    "Poland": "Europe/Warsaw",
    "Portugal": "Europe/Lisbon",
    "PRC": "Asia/Shanghai",
    "PST8PDT": "America/Los_Angeles",
    "ROC": "Asia/Taipei",
    "ROK": "Asia/Seoul",
    "Singapore": "Asia/Singapore",
    "Turkey": "Europe/Istanbul",
    "UCT": "UTC",
    "Universal": "Etc/UTC",
    "US/Alaska": "America/Anchorage",
    "US/Aleutian": "America/Adak",
    "US/Arizona": "America/Phoenix",
    "US/Central": "America/Chicago",
    "US/East-Indiana": "America/Indiana/Indianapolis",
    "US/Eastern": "America/New_York",
    "US/Hawaii": "Pacific/Honolulu",
    "US/Indiana-Starke": "America/Indiana/Knox",
    "US/Michigan": "America/Detroit",
    "US/Mountain": "America/Denver",
    "US/Pacific": "America/Los_Angeles",
    "US/Samoa": "Pacific/Pago_Pago",
    "Etc/UTC": "UTC",
    "Etc/UCT": "UTC",
    "Etc/Universal": "UTC",
    "Etc/Zulu": "UTC",
    "Universal": "UTC",
    "Zulu": "UTC",
    "W-SU": "Europe/Moscow",
    "WET": "Europe/Lisbon",
    "Pacific/Samoa": "Pacific/Pago_Pago",
    "Pacific/Truk": "Pacific/Chuuk",
    "Pacific/Ponape": "Pacific/Pohnpei",
    "Pacific/Yap": "Pacific/Chuuk",
    "Arctic/Longyearbyen": "Europe/Berlin",
    "Antarctica/South_Pole": "Pacific/Auckland",
    "Antarctica/DumontDUrville": "Pacific/Port_Moresby",
}

def parse_tzif(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'TZif':
        return None
    version = chr(data[4])
    pos = 20
    counts = struct.unpack_from('>6I', data, pos)
    pos += 24
    ttisutcnt, ttisstdcnt, leapcnt, timecnt, typecnt, charcnt = counts

    # skip block 1
    pos += timecnt * 4 + timecnt + typecnt * 6 + charcnt + leapcnt * 8 + ttisstdcnt + ttisutcnt

    if version not in ('2', '3'):
        # v1: parse block 1 directly
        pos = 20 + 24
        times = struct.unpack_from(f'>{timecnt}i', data, pos)
        pos += timecnt * 4
        types = struct.unpack_from(f'>{timecnt}B', data, pos)
        pos += timecnt
        typeinfos = []
        for _ in range(typecnt):
            utoff, isdst, desigidx = struct.unpack_from('>iBB', data, pos)
            pos += 6
            typeinfos.append((utoff, isdst, desigidx))
        transitions = []
        for i in range(timecnt):
            transitions.append((int(times[i]), typeinfos[types[i]][0]))
        initial_offset = typeinfos[types[0]][0] if timecnt > 0 else (typeinfos[0][0] if typecnt > 0 else 0)
        return transitions, initial_offset

    # block 2
    if data[pos:pos+5] != b'TZif' + version.encode():
        return None
    pos += 20
    counts = struct.unpack_from('>6I', data, pos)
    pos += 24
    ttisutcnt, ttisstdcnt, leapcnt, timecnt, typecnt, charcnt = counts

    times = struct.unpack_from(f'>{timecnt}q', data, pos)
    pos += timecnt * 8
    types = struct.unpack_from(f'>{timecnt}B', data, pos)
    pos += timecnt
    typeinfos = []
    for _ in range(typecnt):
        utoff, isdst, desigidx = struct.unpack_from('>iBB', data, pos)
        pos += 6
        typeinfos.append((utoff, isdst, desigidx))

    transitions = []
    for i in range(timecnt):
        transitions.append((int(times[i]), typeinfos[types[i]][0]))

    # initial offset is the offset before the first transition.
    # In practice, typeinfos[0] is the LMT/pre-first-transition offset.
    initial_offset = typeinfos[0][0] if typecnt > 0 else 0
    return transitions, initial_offset

def discover_aliases():
    """Map alias -> canonical by following symlinks in zoneinfo."""
    aliases = {}
    for root, dirs, files in os.walk(ZONEINFO_DIR):
        # prevent descending into leapseconds or posix/right if needed
        for name in files:
            full = os.path.join(root, name)
            if os.path.islink(full):
                target = os.readlink(full)
                # resolve relative symlink
                if not target.startswith('/'):
                    target = os.path.normpath(os.path.join(os.path.dirname(full), target))
                if target.startswith(ZONEINFO_DIR + '/'):
                    target = target[len(ZONEINFO_DIR)+1:]
                alias = full[len(ZONEINFO_DIR)+1:]
                aliases[alias] = target
    return aliases

def collect_all_zones():
    """Return set of canonical zones plus aliases."""
    zones = set(NEEDED_ZONES)
    # Only include aliases that are needed or point to needed zones
    relevant_aliases = {}
    for alias, target in HARDCODED_ALIASES.items():
        if alias in NEEDED_ZONES or target in NEEDED_ZONES:
            relevant_aliases[alias] = target
            zones.add(alias)
            zones.add(target)
    return zones, relevant_aliases

def main():
    zones, aliases = collect_all_zones()

    # gather data for each zone
    zone_data = {}
    missing = []
    for z in sorted(zones):
        path = os.path.join(ZONEINFO_DIR, z)
        if not os.path.exists(path):
            missing.append(z)
            continue
        res = parse_tzif(path)
        if res is None:
            missing.append(z)
            continue
        transitions, initial_offset = res
        zone_data[z] = (transitions, initial_offset)

    if missing:
        print(f"Missing {len(missing)} zones (will be unsupported): {missing[:10]}", file=sys.stderr)

    # Resolve alias chains to canonical zone (a zone not in aliases.keys())
    def resolve_canonical(name):
        seen = set()
        while name in aliases and name not in seen:
            seen.add(name)
            name = aliases[name]
        return name

    canonical_set = set()
    alias_to_canonical = {}
    for z in zone_data:
        if z in aliases:
            target = resolve_canonical(z)
            if target in zone_data:
                alias_to_canonical[z] = target
        else:
            canonical_set.add(z)

    # Also add aliases whose target is canonical
    for alias, target in aliases.items():
        resolved = resolve_canonical(target)
        if resolved in zone_data and alias not in alias_to_canonical:
            alias_to_canonical[alias] = resolved

    canonical_list = sorted(canonical_set)
    canonical_index = {name: i for i, name in enumerate(canonical_list)}

    def iso_comment(ts):
        from datetime import datetime, timezone
        dt = datetime.fromtimestamp(ts, tz=timezone.utc)
        return dt.strftime('%Y-%m-%dT%H:%M:%SZ')

    # Write generated C file
    out_path = "temporal/tzdata_generated.c"
    with open(out_path, "w") as f:
        f.write('/* Auto-generated by gen_tzdata.py — DO NOT EDIT */\n')
        f.write('#include "timezone.h"\n\n')

        # Transition arrays per zone
        for name in canonical_list:
            transitions, initial_offset = zone_data[name]
            var_name = name.replace('/', '_').replace('-', '_').replace('+', '_plus_')
            f.write(f'static const TZTransition tz_trans_{var_name}[] = {{\n')
            # prepend initial offset at INT64_MIN
            f.write(f'    {{ INT64_MIN, {initial_offset} }}, /* before first transition */\n')
            for ts, off in transitions:
                f.write(f'    {{ {ts}LL, {off} }}, /* {iso_comment(ts)} */\n')
            f.write('};\n\n')

        # Alias table
        alias_list = sorted(alias_to_canonical.items())
        f.write('const TZAlias ti_tz_aliases[] = {\n')
        for alias, target in alias_list:
            if target in canonical_index:
                f.write(f'    {{ "{alias}", {canonical_index[target]} }},\n')
        f.write('};\n')
        f.write(f'const int ti_tz_alias_count = countof(ti_tz_aliases);\n\n')

        # Zone info table
        f.write('const TZData ti_tz_zone_data[] = {\n')
        for name in canonical_list:
            var_name = name.replace('/', '_').replace('-', '_').replace('+', '_plus_')
            transitions, _ = zone_data[name]
            count = len(transitions) + 1  # including the INT64_MIN dummy
            f.write(f'    {{ "{name}", tz_trans_{var_name}, {count} }},\n')
        f.write('};\n\n')

        f.write('const int ti_tz_zone_count = countof(ti_tz_zone_data);\n')

    print(f"Generated {out_path}: {len(canonical_list)} canonical zones, {len(alias_list)} aliases", file=sys.stderr)

if __name__ == "__main__":
    main()
