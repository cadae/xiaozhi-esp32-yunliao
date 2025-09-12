#!/usr/bin/env python3
import os
import sys
import subprocess
import re

# 全局变量，用于跟踪当前语言
current_lang = None

def read_mac_addresses(file_path):
    mac_addresses = []
    try:
        with open(file_path, 'r') as f:
            for line in f:
                line = line.strip()
                # 验证地址格式 (XX:XX:XX:XX:XX:XX)
                if re.match(r'^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$', line):
                    mac_addresses.append(line)
                else:
                    print(f"警告: 跳过无效的地址格式: {line}")
    except FileNotFoundError:
        print(f"错误: 找不到文件 {file_path}")
        return None
    return mac_addresses

def modify_music_id(mac_address, music_file_path):
    try:
        with open(music_file_path, 'r') as f:
            content = f.read()
        
        # 查找并替换 MUSIC_ID 定义
        pattern = r'// #define MUSIC_ID ""'
        replacement = f'#define MUSIC_ID "{mac_address}"'
        
        if re.search(pattern, content):
            content = re.sub(pattern, replacement, content)
            with open(music_file_path, 'w') as f:
                f.write(content)
            print(f"成功修改 {music_file_path} 中的MUSIC_ID为: {mac_address}")
            return True
        else:
            print(f"错误: 在 {music_file_path} 中找不到 '// #define MUSIC_ID ""' 行")
            return False
    except Exception as e:
        print(f"修改文件时出错: {e}")
        return False

def restore_music_id(music_file_path):
    """恢复esp32_music.cc文件中的MUSIC_ID定义为原始状态"""
    try:
        with open(music_file_path, 'r') as f:
            content = f.read()
        
        # 查找并替换 MUSIC_ID 定义
        pattern = r'#define MUSIC_ID "[0-9A-Fa-f:]+"'
        replacement = '// #define MUSIC_ID ""'
        
        if re.search(pattern, content):
            content = re.sub(pattern, replacement, content)
            with open(music_file_path, 'w') as f:
                f.write(content)
            print(f"成功恢复 {music_file_path} 中的MUSIC_ID为原始状态")
            return True
        else:
            print(f"警告: 在 {music_file_path} 中找不到已修改的MUSIC_ID定义")
            return False
    except Exception as e:
        print(f"恢复文件时出错: {e}")
        return False

def generate_firmware(lang, mac_address):
    """调用原始release.py生成固件"""
    global current_lang
    
    # 去掉地址中的冒号
    mac_suffix = mac_address.replace(':', '')
    
    # 检查语言是否改变
    lang_changed = (current_lang != lang)
    
    # 构建命令
    cmd = [sys.executable, 'scripts/release.py', 'xiaozhiyunliao-s3', '--lang', lang, '--suffix', mac_suffix]
    
    # 如果语言没有改变，添加--skip-set-target参数
    if not lang_changed:
        cmd.append('--skip-set-target')
        print(f"语言未改变，跳过set-target操作")
    else:
        print(f"语言已改变，需要执行set-target操作")
        current_lang = lang  # 更新当前语言
    
    print(f"执行命令: {' '.join(cmd)}")
    
    process = None
    try:
        # 执行命令并实时显示输出
        process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1, universal_newlines=True)
        
        # 实时打印输出
        while True:
            output = process.stdout.readline()
            if output == '' and process.poll() is not None:
                break
            if output:
                print(output.strip())
        
        # 等待进程结束并获取返回码
        return_code = process.wait()
        if return_code == 0:
            print("命令执行成功")
            return True
        else:
            print(f"命令执行失败，返回码: {return_code}")
            return False
    except Exception as e:
        print(f"执行命令时出错: {e}")
        return False
    finally:
        # 确保进程和管道被正确关闭，释放内存
        if process:
            # 确保进程已经结束
            if process.poll() is None:
                process.terminate()
                try:
                    # 给进程一些时间来正常终止
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    # 如果进程不终止，强制杀死
                    process.kill()
            # 关闭标准输出管道
            if process.stdout:
                process.stdout.close()

def main():
    # 切换到项目根目录
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    os.chdir(project_root)
    
    # 定义文件路径
    mac_cn_file = os.path.join("..", "xiaozhi-fonts", "MAC_cn.txt")
    mac_tw_file = os.path.join("..", "xiaozhi-fonts", "MAC_tw.txt")
    music_file = os.path.join("main", "boards", "common", "esp32_music.cc")
    
    # 读取地址
    cn_macs = read_mac_addresses(mac_cn_file)
    tw_macs = read_mac_addresses(mac_tw_file)
    
    if cn_macs is None or tw_macs is None:
        print("错误: 无法读取地址文件")
        sys.exit(1)
    
    if not cn_macs and not tw_macs:
        print("错误: 没有找到有效的地址")
        sys.exit(1)
    
    print(f"找到 {len(cn_macs)} 个中文地址和 {len(tw_macs)} 个繁体中文地址")
    
    # 处理中文地址
    for mac in cn_macs:
        print(f"\n处理中文地址: {mac}")
        
        # 修改MUSIC_ID
        if not modify_music_id(mac, music_file):
            continue
        
        # 生成固件
        if generate_firmware("cn", mac):
            print(f"成功为地址 {mac} 生成中文固件")
        else:
            print(f"为地址 {mac} 生成中文固件失败")
        
        # 恢复MUSIC_ID
        restore_music_id(music_file)
    
    # 处理繁体中文地址
    for mac in tw_macs:
        print(f"\n处理繁体中文地址: {mac}")
        
        # 修改MUSIC_ID
        if not modify_music_id(mac, music_file):
            continue
        
        # 生成固件
        if generate_firmware("tw", mac):
            print(f"成功为地址 {mac} 生成繁体中文固件")
        else:
            print(f"为地址 {mac} 生成繁体中文固件失败")
        
        # 恢复MUSIC_ID
        restore_music_id(music_file)
    
    print("\n所有地址处理完成")

if __name__ == "__main__":
    main()