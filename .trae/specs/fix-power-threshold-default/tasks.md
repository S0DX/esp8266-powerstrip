# Tasks

- [x] Task 1: 修改功率阈值加载逻辑，无效时写入默认值
  - [x] SubTask 1.1: 检测 EEPROM 值无效时（0 或 >200），设置为 0.5f 并写入 EEPROM
  - [x] SubTask 1.2: 添加调试日志，记录写入默认值

- [x] Task 2: 对计费供电阈值应用相同逻辑
  - [x] SubTask 2.1: 检测 EEPROM 值无效时，设置为 10.0f 并写入 EEPROM

- [x] Task 3: 编译验证
  - [x] SubTask 3.1: 运行 pio run -e esp12e 确认编译通过

# Task Dependencies
- Task 2 可并行于 Task 1
- Task 3 depends on Task 1, 2