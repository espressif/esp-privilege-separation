#
# SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
#

import re
import os
import sys
import json
import argparse
try:
    import idf_size
except ImportError:
    idf_path = os.getenv("IDF_PATH")
    if idf_path:
        idf_tools_path = os.path.join(idf_path, 'tools')
        sys.path.insert(0, idf_tools_path)
    import idf_size

def get_libs_list(map_file):
    map_file = open(map_file, "r")

    detected_target, segments, sections = idf_size.load_map_data(map_file)
    map_file.close()
    idf_size.check_target(detected_target, map_file)

    segments_diff, sections_diff, detected_target_diff = {}, {}, ''

    archive_output = idf_size.get_detailed_sizes(sections, 'archive', 'Archive File', True, sections_diff)
    obj_output = idf_size.get_detailed_sizes(sections, 'file', 'Object File', True, sections_diff)

    archive_dict = json.loads(archive_output)
    obj_dict = json.loads(obj_output)


    for key in archive_dict.keys():
        archive_obj_files = [re.sub(r'^.*?:', '', s) for s in obj_dict.keys() if key in s]
        archive_dict[key]['object_files'] = archive_obj_files

    return archive_dict

def main():
    parser = argparse.ArgumentParser(description='A tool to generate protected and user app libraries in privilege-separation app')
    parser.add_argument(
        '--build',
        required=True,
        help='Path to build directory'
    )
    parser.add_argument(
        '--app_name',
        required=True,
        help='Application name'
    )
    args = parser.parse_args()

    with open(os.path.join(args.build, 'app_libs_and_objs.json'), 'w') as f:
        json.dump({'Protected app libs' : get_libs_list(os.path.join(args.build, args.app_name + '.map')),
                    'User app libs' : get_libs_list(os.path.join(args.build, 'user_app', 'user_app.map'))},
                    f,
                    allow_nan=True,
                    indent=idf_size.GLOBAL_JSON_INDENT,
                    separators=idf_size.GLOBAL_JSON_SEPARATORS)

if __name__ == '__main__':
    main()
