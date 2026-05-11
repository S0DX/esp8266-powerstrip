# Tasks

- [x] Task 1: 重构 WiFiManager::init() 分离 AP 和 STA
  - [x] SubTask 1.1: 移除 init() 中的 WiFi.begin() 调用
  - [x] SubTask 1.2: 添加 sta_connect_pending_ 标志，在 init() 中设置为 true
  - [x] SubTask 1.3: 在 init() 的 AP 启动后增加 3 次 yield() 调用

- [x] Task 2: 修改 handle() 处理非阻塞 STA 连接
  - [x] SubTask 2.1: 在 handle() 中检查 sta_connect_pending_ 标志
  - [x] SubTask 2.2: 如果标志为 true，调用 WiFi.begin() 并清除标志
  - [x] SubTask 2.3: 确保 handle() 开头就调用 yield()

- [x] Task 3: 编译验证
  - [x] SubTask 3.1: 运行 pio run -e esp12e 确认编译通过

# Task Dependencies
- Task 2 depends on Task 1
- Task 3 depends on Task 1, 2