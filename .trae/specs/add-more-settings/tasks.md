# Tasks

- [x] Task 1: 在系统设置卡片中添加更多设置按钮
  - [x] SubTask 1.1: 在 AP 热点密码输入框下方添加按钮
  - [x] SubTask 1.2: 添加 onclick 事件调用 showMoreSettings()

- [x] Task 2: 创建二级页面模态框
  - [x] SubTask 2.1: 添加模态框 HTML 结构（全屏覆盖，带关闭按钮）
  - [x] SubTask 2.2: 将设备信息后的 6 个卡片移到模态框内
  - [x] SubTask 2.3: 添加 showMoreSettings() 和 closeMoreSettings() 函数

- [x] Task 3: 更新版本号为 2.0
  - [x] SubTask 3.1: 修改 config.h 中的 VERSION 常量
  - [x] SubTask 3.2: 验证 Web 页面显示正确

- [x] Task 4: 编译验证
  - [x] SubTask 4.1: 运行 pio run -e esp12e 确认编译通过

# Task Dependencies
- Task 2 depends on Task 1
- Task 3 独立
- Task 4 depends on Task 1, 2, 3
