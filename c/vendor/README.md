# vendor —— 第三方头文件，逐字不改

## `onnxruntime_c_api.h`

- **来源**：https://raw.githubusercontent.com/microsoft/onnxruntime/v1.22.0/include/onnxruntime/core/session/onnxruntime_c_api.h
- **版本**：ONNX Runtime v1.22.0（`ORT_API_VERSION` = 22）
- **许可**：MIT（文件头保留了微软的版权声明）
- **改动**：无，逐字节照抄（含 BOM）。升级就整份替换，不要局部打补丁。

**为什么 vendor 而不是依赖系统头**：`sa_onnx.c` 只用 `LoadLibraryW` / `dlopen` 在运行时解析
`OrtGetApiBase`，**不链接 ORT 的 import lib** —— 所以构建期唯一需要的就是这个头文件。
交叉编译到 Windows（renkolab 的 `xwin` 那套）因此一行链接参数都不用加。

**版本兼容**：运行时按 `ORT_API_VERSION` 往下试到 `SA_ONNX_MIN_API`（见 `sa_onnx.c`），
所以比 v1.22 新的 `onnxruntime.dll` 直接可用，稍旧的也能用 —— 我们只用一批很老的 API。
