# Tasks

- [x] Task 1: 移除分页机制，合并为单页 UI
  - [x] SubTask 1.1: 移除 CSS 中 `.page`、`.page.active`、`.pages`、`.tab-bar`、`.tab-item` 相关样式
  - [x] SubTask 1.2: 移除 HTML 中的 `<div class="page active" id="page1">`、`<div class="page" id="page2">` 包裹层和 tab-bar HTML
  - [x] SubTask 1.3: 移除 header 中的 `pageTitle` span 元素
  - [x] SubTask 1.4: 移除 JS 中的 `switchPage()` 函数、滑动切换 touchstart/touchend 事件监听
  - [x] SubTask 1.5: 移除 `eData`、`eLabel` 未使用变量（eLabel 仅在 init 中赋值但从未读取）

- [x] Task 2: 清理卡片排布冗余空行
  - [x] SubTask 2.1: 逐行检查所有 `</div>` 与下一个 `<div class="card">` 之间，确保恰好一个空行，删除连续空行
  - [x] SubTask 2.2: 检查卡片内部结构，移除不必要的空行

- [x] Task 3: 修复人来上电前端状态显示 bug
  - [x] SubTask 3.1: 将 update() 中 `d.target` 改为 `d.wifiDetectTarget`（与后端 /api/status 返回的字段名一致）
  - [x] SubTask 3.2: 验证前端 wifiDetectTarget 显示与后端返回值对应

- [x] Task 4: 编译验证
  - [x] SubTask 4.1: 运行 `pio run -e esp12e` 确认编译通过

# Task Dependencies
- Task 2 depends on Task 1（合并单页后再清理空行）
- Task 3 独立于 Task 1/2，可并行
- Task 4 depends on Task 1, 2, 3
