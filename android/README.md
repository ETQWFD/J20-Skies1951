# 长空·1951 · J-20 SKIES OVER KOREA

一款用 **C（raylib / OpenGL）** 编写的跨平台 3D 纪念向射击游戏。玩家可驾驶 **歼-20** 在抗美援朝题材的
朝鲜半岛地形上空与全 AI 美军 F-86 机群、地面装甲部队作战；也可切换到 **第一人称志愿军步兵**，
与 AI 战友一起夺取高地。结局页写入抗美援朝历史与开发说明。

- 程序化 3D 模型：歼-20、F-86、坦克/卡车/高炮、士兵、枪械、导弹/航弹、红旗
- 柏林噪声（fBm/ridged）生成山地、森林、雪原与海面
- 自定义光照着色器（Lambert + 半球环境光 + 距离雾），**每次启动运行时编译着色器**
- 火焰、爆炸、烟雾、曳光弹粒子；程序化枪声、爆炸、冲锋号音效
- 本地规则/状态机 AI（敌机四波、地面装甲、美军步兵、志愿军战友）
- 多结局（全胜、夺取制空权、带伤返航、壮烈牺牲等），结局含历史与开发说明

## 一、直接游玩

### Windows（bin/J20_Skies1951.exe）
- 64 位 Windows（x86_64），免安装、绿色单文件，仅依赖系统自带 DLL。
- 双击 `J20_Skies1951.exe` 运行（会同时出现一个控制台日志窗口，属正常现象）。
- 需要支持 OpenGL 3.3 的显卡/驱动（绝大多数 2012 年后的设备均可）。

### Android（bin/J20_Skies1951.apk）
- 一个 APK 同时包含 **arm64-v8a（64 位）+ armeabi-v7a（32 位）** 原生库，最低系统 **Android 7.0（API 24）**，新老手机、32 位机都能装。
- 安装：把 APK 传到手机，点击安装，首次需在系统设置中允许“安装未知来源应用”。
- **分享给朋友提示“安装包损坏/解析失败”怎么办**：多为传输工具改包所致，并非文件坏了：
  - 用**微信**直接发 `.apk` 常被改名为 `xxx.apk.1` 或拦截直装。请改发本项目里的
    `J20_Skies1951_安卓安装包.zip`，对方在手机上**解压后**再点里面的 `J20_Skies1951.apk` 安装；
  - 或让对方在文件管理里把收到的文件名末尾 `.1` 去掉、恢复成 `.apk` 再装；
  - 也可用 QQ「发送文件」、蓝牙、网盘链接或 U 盘传输，避免二次改名；
  - 本 APK 已通过 apksigner v2/v3 校验、zip 完整，校验值见发布说明（可核对有没有被改坏）。
- 已用生成的证书签名，包名 `com.changkong1951.skies`，应用名“长空·1951”，横屏全屏。
- 触屏操作：
  - 左下虚拟摇杆：只负责移动（陆战，向上推＝前进）/ 俯仰·滚转（空战，自动巡航油门）；摇杆本身不会开火
  - 右半屏拖动：陆战转动视角，点/拖屏幕**不会**触发射击
  - 右下 `火`：**只有按住这个键才开火**（机炮 / 步枪连发）
  - `镜`：陆战按住打开机瞄 / 狙击镜（仅陆战出现该键）
  - `弹`：导弹（空战）/ 跳跃（陆战）；`炸`：投弹（空战）/ 装填（陆战）
  - `换`：切换步枪 / 冲锋枪
  - 左上角“暂停”：弹出暂停菜单（游戏完全冻结），可点「继续 / 返回主菜单」；空战、陆战均可
  - 电脑端同样可暂停：`P` 暂停、`Enter` 继续、`Q` 返回菜单，`Esc` 直接退出

### Linux（bin/J20_Skies1951）
```bash
chmod +x bin/J20_Skies1951
./bin/J20_Skies1951
# 无显示器自检（自动演示并生成截图）：
LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe ./bin/J20_Skies1951 --selftest
```

## 二、键盘 / 鼠标操作

### 空战（驾驶歼-20）
| 操作 | 按键 |
|---|---|
| 油门加减 | W / S |
| 俯仰 | ↑ / ↓ |
| 滚转 | A / D |
| 偏航 | Q / E |
| 机炮 | 空格（按住） |
| 发射导弹 | F |
| 投弹 | B |
| 低空慢速返航降落 | H |
| 返回菜单 | Esc |

### 陆战（第一人称步兵）
| 操作 | 按键 |
|---|---|
| 移动 | W A S D |
| 冲刺 | Shift |
| 跃进 | 空格 |
| 视角 | 鼠标移动（右移即右转） |
| 射击 | 鼠标左键 |
| 机瞄 / 狙击镜 | 鼠标右键按住（视场放大 + 镜圈刻度） |
| 莫辛纳甘 / 波波沙切换 | 1 / 2 |
| 装填 | R |
| 返回菜单 | Esc |

主菜单按 `1/2/3` 进入空战、陆战、操作说明/历史。

## 三、从源码构建

源码为纯 C，引擎使用内置的 raylib 5.5（已附带各平台预编译静态库，也可用 third_party/raylib 源码重建）。

### Linux
```bash
make linux        # 产物 build/J20_Skies1951
# 依赖：gcc、cmake、libX11/Xrandr/Xi/Xcursor/Xinerama、libasound2、GL
```

### Windows（在 Linux 上用 mingw 交叉编译）
```bash
make windows \
  CFLAGS="-Os -std=c11 -DFONT_EMBEDDED -Wno-unused-variable -Wno-unused-function \
  -Wno-unused-but-set-variable -Wno-unused-result -Wno-misleading-indentation"
# 产物 build/J20_Skies1951.exe
# 依赖：gcc-mingw-w64-x86-64
```

### Android APK（不依赖 gradle/Android Studio）
脚本 `android/build_apk.sh` 使用 NDK clang 直接编译为 `libmain.so`，再用
aapt + d8 + zipalign + apksigner 打包签名。需准备：
- Android NDK（r25c 验证），默认路径 `/home/user/android/android-ndk-r25c`
- SDK `build-tools`（34.0.0 验证）与 `platforms/android-33`（脚本内路径可改）
- JDK（17 验证）

按脚本顶部变量修改路径后：
```bash
bash android/build_apk.sh   # 产物 android/out/J20_Skies1951.apk
```
当前只编译 arm64-v8a；如需 armeabi-v7a/x86_64，按同样方式用对应 NDK target 再编一份
raylib 静态库与游戏 .so，并在 `lib/<abi>/` 下放入即可（脚本可参数化扩展）。

### 内嵌中文字体
`third_party/font/gamefont.ttf` 是文泉驿微米黑的子集（仅包含游戏用字）。
新增中文后重新生成：
```bash
python3 tools/genfont.py
pyftsubset /usr/share/fonts/truetype/wqy/wqy-microhei.ttc --font-number=0 \
  --unicodes="$(paste -sd, build/unicodes.txt)" --output-file=third_party/font/gamefont.ttf
# 再用 objcopy 生成各平台 fontdata_*.o（见 Makefile 与 android/build_apk.sh）
```

## 四、说明与边界（如实告知）
- 本作为历史纪念与技术演示的**可玩原型**，AI 为本地规则/状态机实现，未接入任何在线大模型，
  也未在程序中硬编码任何在线 API 密钥。
- 帧率未设上限（`SetTargetFPS(0)`），实际帧率取决于设备；高刷屏可达到很高帧率，但不保证 480 FPS。
- VR 需对应 OpenXR/立体渲染后端，当前桌面/安卓构建未内置，属可扩展项。
- APK 在构建环境中已完成编译、链接、签名、对齐与结构校验（入口 `ANativeActivity_onCreate`、
  依赖均为系统库），但本环境无安卓真机/模拟器，未能做真机画面回归；如遇兼容问题可反馈机型。

铭记历史 · 珍爱和平 · 吾辈自强。
