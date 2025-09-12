import sys
import os
import json
import zipfile
import argparse  # 添加argparse导入

# 切换到项目根目录
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

def get_board_type():
    with open("build/compile_commands.json") as f:
        data = json.load(f)
        for item in data:
            if not item["file"].endswith("main.cc"):
                continue
            command = item["command"]
            # extract -DBOARD_TYPE=xxx
            board_type = command.split("-DBOARD_TYPE=\\\"")[1].split("\\\"")[0].strip()
            return board_type
    return None

def get_project_version():
    with open("CMakeLists.txt") as f:
        for line in f:
            if line.startswith("set(PROJECT_VER"):
                return line.split("\"")[1].split("\"")[0].strip()
    return None

def get_chip_suffix(target):
    """根据 target 获取芯片后缀"""
    if target == "esp32s3":
        return "_s3"
    elif target == "esp32c3":
        return "_c3"
    else:
        return ""

def merge_bin(lang_suffix="", chip_suffix="", suffix=""):
    full_suffix = f"{chip_suffix}{lang_suffix}" if chip_suffix or lang_suffix else ""
    merge_output = f"merged_yunliao{full_suffix}{suffix}.bin" if full_suffix or suffix else "merged-yunliao.bin"
    if os.system(f"idf.py merge-bin -o {merge_output}") != 0:
        print("merge bin failed")
        sys.exit(1)

def copy_firmware(lang_suffix="", chip_suffix=""):
    if not os.path.exists("releases"):
        os.makedirs("releases")
    
    full_suffix = f"{chip_suffix}{lang_suffix}" if chip_suffix or lang_suffix else ""
    bin_name = f"yunliao{full_suffix}.bin" if full_suffix else "yunliao.bin"
    source_path = "build/xiaozhi.bin"
    dest_path = f"releases/{bin_name}"
    
    if os.system(f"cp {source_path} {dest_path}") != 0:
        print(f"copy {source_path} to {dest_path} failed")
        sys.exit(1)
    print(f"copied {source_path} to {dest_path}")

def zip_bin(board_type, project_version, lang_suffix="", chip_suffix="", suffix=""):
    if not os.path.exists("releases"):
        os.makedirs("releases")
    
    # 根据语言后缀和芯片后缀生成输入文件名和输出路径
    full_suffix = f"{chip_suffix}{lang_suffix}" if chip_suffix or lang_suffix else ""
    bin_name = f"merged_yunliao{full_suffix}{suffix}.bin" if full_suffix or suffix else "merged-yunliao.bin"
    input_path = f"build/{bin_name}"
    output_path = f"releases/v{project_version}_yunliao{full_suffix}{suffix}.zip"
    
    if os.path.exists(output_path):
        os.remove(output_path)
    with zipfile.ZipFile(output_path, 'w', compression=zipfile.ZIP_DEFLATED) as zipf:
        zipf.write(input_path, arcname=bin_name)
    print(f"zip bin to {output_path} done")
    

def release_current(suffix=""):
    board_type = get_board_type()
    print("board type:", board_type)
    project_version = get_project_version()
    print("project version:", project_version)
    
    # 获取 target 和 chip_suffix
    with open(f"main/boards/{board_type}/config.json", "r") as f:
        config = json.load(f)
    target = config["target"]
    chip_suffix = get_chip_suffix(target)
    
    merge_bin(chip_suffix=chip_suffix, suffix=suffix)
    zip_bin(board_type, project_version, chip_suffix=chip_suffix, suffix=suffix)

def get_all_board_types():
    board_configs = {}
    with open("main/CMakeLists.txt", encoding='utf-8') as f:
        lines = f.readlines()
        for i, line in enumerate(lines):
            # 查找 if(CONFIG_BOARD_TYPE_*) 行
            if "if(CONFIG_BOARD_TYPE_" in line:
                config_name = line.strip().split("if(")[1].split(")")[0]
                # 查找下一行的 set(BOARD_TYPE "xxx") 
                next_line = lines[i + 1].strip()
                if next_line.startswith("set(BOARD_TYPE"):
                    board_type = next_line.split('"')[1]
                    board_configs[config_name] = board_type
    return board_configs

def release(board_type, board_config, lang_suffix="", suffix="", skip_set_target=False):
    # 配置文件路径
    config_path = f"main/boards/{board_type}/config{lang_suffix}.json"
    
    if not os.path.exists(config_path):
        print(f"跳过 {board_type} 因为 config{lang_suffix}.json 不存在")
        return

    # Print Project Version
    project_version = get_project_version()
    print(f"Project Version: {project_version}", config_path)

    with open(config_path, "r") as f:
        config = json.load(f)
    target = config["target"]
    builds = config["builds"]
    
    # 获取芯片后缀
    chip_suffix = get_chip_suffix(target)
    
    for build in builds:
        name = build["name"]
        if not name.startswith(board_type):
            raise ValueError(f"name {name} 必须以 {board_type} 开头")
        output_path = f"releases/v{project_version}_{name}{chip_suffix}{lang_suffix}{suffix}.zip"
        if os.path.exists(output_path):
            print(f"跳过 {board_type} 因为 {output_path} 已存在")
            continue

        sdkconfig_append = [f"{board_config}=y"]
        for append in build.get("sdkconfig_append", []):
            sdkconfig_append.append(append)
        print(f"name: {name}")
        print(f"target: {target}")
        for append in sdkconfig_append:
            print(f"sdkconfig_append: {append}")
        
        # 如果不跳过set-target，则执行set-target操作
        if not skip_set_target:
            # unset IDF_TARGET
            os.environ.pop("IDF_TARGET", None)
            # Call set-target
            if os.system(f"idf.py set-target {target}") != 0:
                print("set-target failed")
                sys.exit(1)
        else:
            print("跳过set-target操作")
            
        # Append sdkconfig
        with open("sdkconfig", "a") as f:
            f.write("\n")
            for append in sdkconfig_append:
                f.write(f"{append}\n")
        # Build with macro BOARD_NAME defined to name
        if os.system(f"idf.py -DBOARD_NAME={name} build") != 0:
            print("build failed")
            sys.exit(1)
        # 复制固件文件
        copy_firmware(lang_suffix, chip_suffix)
        # Call merge-bin
        merge_bin(lang_suffix, chip_suffix, suffix)
        # Zip bin
        zip_bin(name, project_version, lang_suffix, chip_suffix, suffix)
        print("-" * 80)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("board", nargs="?", default=None, help="板子类型或 all")
    parser.add_argument("-c", "--config", default="config.json", help="指定 config 文件名，默认 config.json")
    parser.add_argument("--lang", help="语言后缀参数（如：cn/en/jp），自动生成对应配置文件")
    parser.add_argument("--suffix", help="自定义后缀，将添加到merge_bin的merge_output和zip_bin的output_path中")
    parser.add_argument("--list-boards", action="store_true", help="列出所有支持的 board 列表")
    parser.add_argument("--json", action="store_true", help="配合 --list-boards，JSON 格式输出")
    parser.add_argument("--skip-set-target", action="store_true", help="跳过set-target操作")
    args = parser.parse_args()

    # 生成语言后缀
    lang_suffix = f'_{args.lang}' if args.lang else ''
    # 生成自定义后缀
    suffix = f'_{args.suffix}' if args.suffix else ''

    if args.list_boards:
        board_configs = get_all_board_types()
        boards = list(board_configs.values())
        if args.json:
            print(json.dumps(boards))
        else:
            for board in boards:
                print(board)
        sys.exit(0)

    if args.board:
        board_configs = get_all_board_types()
        found = False
        for board_config, board_type in board_configs.items():
            if sys.argv[1] == 'all' or board_type == sys.argv[1]:
                release(board_type, board_config, lang_suffix, suffix, args.skip_set_target)
                found = True
        if not found:
            print(f"未找到板子类型: {sys.argv[1]}")
            print("可用的板子类型:")
            for board_type in board_configs.values():
                print(f"  {board_type}")
    else:
        release_current(suffix)
