在终端执行以下命令：
# 1. 设置ESP-IDF环境变量
source ~/.espressif/v5.5.1/esp-idf/export.sh

# 2. 验证编译器已在PATH中
which xtensa-esp32s3-elf-gcc

# 3. 重新构建项目
cd "/Users/tarrywang/Library/CloudStorage/OneDrive-个人/项目/EPS32-CozeAgent"
idf.py build


永久解决方案（可选）
如果每次都要手动source很麻烦，可以添加到shell配置文件：
# 编辑 ~/.zshrc 或 ~/.bash_profile
echo 'alias get_idf="source ~/.espressif/v5.5.1/esp-idf/export.sh"' >> ~/.zshrc

# 重新加载配置
source ~/.zshrc

# 之后只需运行
get_idf