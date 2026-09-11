# ADOFAI Mod Loader（安卓 · 版本 3.3.1）

在《冰与火之舞》安卓版 **3.3.1** 的「设置 → 高级」里新增一个**「添加 Mod」**按钮
（仿「清除游戏数据」样式）。点击后通过系统文件选择器选择 `.zip`，
加载器校验后自动解压并加载根目录的 `.dll` / `.so`。

> ⚠️ 重要事实：**3.3.1 安卓版使用 IL2CPP 后端，原版 APK 无法 `Assembly.Load` 一个 C# `.dll`**。
> 因此本工程采用**双模加载**：
> - **`.so`（推荐，原版即可用）**：`dlopen` + 调用 `Init()`
> - **`.dll`（需游戏工程集成 HybridCLR 方可生效）**：走 `RuntimeApi.LoadMetadataForAOTAssembly`
>
> 你现在用 `.so` Mod 就能在原版 3.3.1 上跑；将来接了 HybridCLR，`.dll` 自动可用。
> 这是技术限制，不是工程缺陷——IL2CPP 把所有 C# 编译成了 native 代码，没有 Mono 运行时。

---

## 一、功能对照（你的需求 → 实现）

| 需求 | 实现位置 | 状态 |
|---|---|---|
| 设置-高级添加「添加 Mod」按钮 | `ModBridge.cs` 克隆清除数据按钮 + `hook.c` 注入 | ✅ 代码完成 |
| 点击打开文件选择 | `ModHost.openModFilePicker` (SAF `ACTION_OPEN_DOCUMENT`) | ✅ |
| 除 zip 外都不支持 | `mod_check_extension` 仅放行 `.zip` | ✅ 已测试 |
| 双击打开并跳到文件 | Activity `onActivityResult` → `loadMod` | ✅ |
| 获取 zip 内 dll 与附带资源 | `extract_root_entries` 解压根目录条目 | ✅ 已测试 |
| 加载 dll | `load_one_dll` → Java `loadDllHybridCLR` | ⚠️ 需 HybridCLR |
| **dll 必须放 zip 根目录** | `extract_root_entries` 只处理根目录文件 | ✅ 已测试 |
| **根目录有文件夹 → 提示** | `mod_check_root_clean` | ✅ 已测试 |

---

## 二、工程结构

```
adofai_mod_loader/
├── CMakeLists.txt              # CMake 构建（NDK 用）
├── build.sh                   # 一键编译 arm64-v8a so
├── build-test/                # 主机本地验证（无需 NDK）
│   ├── test_main.c            #   → 12/12 通过
│   └── jni_stub.h            #   jni.h 桩
├── core/
│   ├── mod_loader.h           # 对外 API
│   ├── mod_loader.c           # 核心：校验 + 解压 + 加载（依赖 zlib）
│   ├── zipio.c / .h          # 自包含 zip 读写（无 minizip 依赖）
│   └── hook.c / .h           # Dobby inline hook 框架
├── jni/
│   └── jni_entry.c            # JNI_OnLoad + RegisterNatives
├── java/com/adofai/mod/
│   └── ModHost.java           # Java 宿主：SAF + 校验 + 通知 + HybridCLR 入口
└── csharp/
    └── ModBridge.cs           # C#：注入按钮 + 调 Java
```

---

## 三、构建 so（需要 Android NDK）

```bash
# 1. 准备环境
export ANDROID_NDK_HOME=/path/to/android-ndk-r25b
sudo apt install zlib1g-dev   # 主机测试用；NDK 自带 libz

# 2. 放入 Dobby（可选，仅 hook 需要）
#    git clone https://github.com/jmpews/Dobby third_party/dobby
#    不放入则仅禁用 hook.c 的 Dobby 部分，mod_loader 仍可编译

# 3. 编译
cd adofai_mod_loader
./build.sh
# 产物: libadofai_mod.so (arm64-v8a)
```

> 沙盒无法联网下载 NDK，故本轮**未在此直接产出 .so**；核心 C 已用主机 gcc 验证
> （`build-test/test_main` → 12 passed），移植到 NDK 仅需 `build.sh` 一键编译。

### 主机本地验证（无需 NDK，已通过）

```bash
cd build-test && gcc -O2 -I../core -I. -DADOFAI_NO_JNI -o test_main test_main.c ../core/zipio.c -lz
./test_main
# === 12 passed, 0 failed ===
```

---

## 四、集成到原版 APK（改 APK，非 Root）

### 步骤 1：反编译
```bash
apktool d -o apk A_Dance_of_Fire_and_Ice_3.3.1.apk
# 取出基准文件（记录版本 hash，用于校验一致性）
cp apk/lib/arm64-v8a/libil2cpp.so .
cp apk/assets/bin/Data/Managed/Metadata/global-metadata.dat .   # 路径按实际调整
```

### 步骤 2：Dump IL2CPP 偏移
```bash
Il2CppDumper libil2cpp.so global-metadata.dat dump/
# 得到 dump.cs —— 在里搜 Settings / Advanced / ClearData
# 找到「高级设置面板初始化」方法的 RVA，填入 hook.c 的
#   #define SETTINGS_ADVANCED_INIT_RVA 0xXXXXXX
```

### 步骤 3：插入 so
```bash
# 把编译好的 so 放进 APK 的 arm64 lib 目录
cp libadofai_mod.so apk/lib/arm64-v8a/

# 让游戏启动时加载它：二选一
#   A) 改 AndroidManifest.xml 的 Application，加 android:hasCode="true" + 自己的 Application 类
#      （在 Application.attachBaseContext 里 System.loadLibrary("adofai_mod")）
#   B) 修改 libmain.so / libunity.so 的 init 数组，插入构造函数
#      （用 IDA 找 __attribute__((constructor)) 段，推荐 A 更稳）
```

### 步骤 4：注入 Java 类
```bash
# 把 ModHost.java 编译成 smali，或整个 classes.dex 合并进 APK
# 最简：用 Android Studio 建空壳 app，把 ModHost 编进 dex，apktool b 合并
```

### 步骤 5：重打包 & 签名
```bash
apktool b -o unsigned.apk apk
zipalign -p 4 unsigned.apk aligned.apk
apksigner sign --ks your.keystore --ks-key-alias key aligned.apk -out final.apk
```

### 步骤 6：绕过完整性校验（关键）
3.3.1 大概率有签名/资源校验，闪退时：
- 搜 `Signature` / `checkSignature` / `integrity` 关键字，NOP 掉
- 或用 LSPosed 模块在运行时 hook 校验方法（需 Root，见下）

---

## 五、获取版本偏移（Il2CppDumper）

每次游戏更新偏移会变，**必须针对 3.3.1 重新 dump**：

1. `Il2CppDumper libil2cpp.so global-metadata.dat output/`
2. 打开 `output/dump.cs`，搜索：
   - `class SettingsController` / `OptionsMenu` / `AdvancedSettings`
   - `ClearGameData` / `清除游戏数据`（定位按钮样式模板）
3. 记下目标方法的 `// RVA: 0xXXXXXX`
4. 填入 `hook.c` 的对应 `#define`

> 若游戏有**函数名混淆**或**元数据加密**，用 **Zygisk-Il2CppDumper** 在运行时 dump
> （需 Root + LSPosed），可绕过静态加密。

---

## 六、让 `.dll` Mod 真正可用（HybridCLR 接入）

若你坚持要让用户上传 `.dll` 直接生效，必须**改游戏 Unity 工程**：

1. 用 Unity（与游戏同版本）新建工程，导入 **HybridCLR**
2. 在 `RuntimeInitializeOnLoad` 里初始化解释器：
   ```csharp
   RuntimeApi.LoadMetadataForAOTAssembly(dllBytes, HomologousImageMode.SuperSet);
   Assembly ass = Assembly.Load(dllBytes);
   ass.GetType("MyMod")?.GetMethod("Init")?.Invoke(null, null);
   ```
3. 重新出 IL2CPP 包 → 替换原版
4. 此时 `ModHost.loadDllHybridCLR` 才能真正加载 `.dll`

**这是让 .dll 在原版游戏运行的唯一方式**；否则请接受 `.so` Mod。

---

## 七、测试矩阵（已验证）

| 用例 | 输入 | 期望 | 结果 |
|---|---|---|---|
| 合法 zip | `good.zip` (Mod.dll + icon.png) | 通过校验，解压 2 项 | ✅ |
| 空目录 | `dirty1.zip` (folder/) | 提示"不应包含文件夹" | ✅ |
| 套文件夹 | `dirty2.zip` (inner/Mod.dll) | 提示"DLL 须放根目录" | ✅ |
| 混合 | `mixed.zip` (Mod.dll + assets/) | 拒绝（assets 在子目录） | ✅ |
| 非法格式 | `.rar` `.7z` 无后缀 | 提示"仅支持 .zip" | ✅ |
| 损坏文件 | 不存在的 zip | 提示"无法打开" | ✅ |

---

## 八、常见问题

**Q: 改完 APK 打开闪退？**
A: 先看 logcat `adb logcat | grep ADOFAI`。90% 是偏移填错 / 校验拦截。

**Q: 按钮没出现？**
A: `SETTINGS_ADVANCED_INIT_RVA` 未填或填错；先用 `auto_dump_il2cpp` 自动遍历类型定位。

**Q: `.dll` 加载提示"需 HybridCLR"？**
A: 预期行为。原版 IL2CPP 无 Mono。改用 `.so` Mod 或接 HybridCLR。

**Q: 推荐哪种 Mod 形式？**
A: **`.so`**。Mod 作者用 NDK 写 C++，导出 `Init()` 即可，原版即插即用。
