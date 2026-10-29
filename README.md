# LVGL 液态玻璃

这是一个由 AI 协助完成的 C/LVGL 液态玻璃效果移植项目，视觉和交互参考了本机上的 AndroidLiquidGlass-kmp 项目：
https://github.com/Kyant0/AndroidLiquidGlass/

## LVGL 侧实现

参考项目是 AndroidLiquidGlass-kmp 的 Liquid Glass (Backdrop) 组件库和示例目录。适配时主要参考了 `LiquidButton`、`LiquidBottomTabs` 和 `ControlCenterContent`，源码位于：

```text
app/src/commonMain/kotlin/com/kyant/backdrop/catalog/
```

Compose 实现不是本项目的运行时依赖。这里将它的背景采样、透镜折射、高光、拖动形变和弹簧反馈，按 LVGL 的 C 对象、事件、定时器、canvas 与 snapshot 接口重新实现。因此实际渲染和动画时序由 LVGL 决定，可能会随屏幕尺寸或刷新率与参考项目有所不同。


## LVGL 配置

液态玻璃渲染器会将背景快照和每个玻璃控件存入 `LV_COLOR_FORMAT_ARGB8888` 缓冲区。因此，即使显示器使用 RGB565，也要开启软件绘制器的 ARGB8888 支持。无需为了液态玻璃把默认显示格式改成 ARGB8888。

CPU 液态玻璃渲染所需配置：

```c
#define LV_USE_MATRIX 1
#define LV_USE_SNAPSHOT 1
#define LV_USE_DRAW_SW 1

#if LV_USE_DRAW_SW
#define LV_DRAW_SW_SUPPORT_ARGB8888 1
#endif

#define LV_USE_IMAGE 1
#define LV_USE_CANVAS 1
```

当前演示还使用以下 LVGL 控件：

```c
#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1
#define LV_USE_BAR 1
#define LV_USE_SLIDER 1
```

演示和控件直接使用以下 Montserrat 字体尺寸：

```c
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
```

当前默认显示格式为 `LV_COLOR_FORMAT_RGB565`，可以保持不变，因为玻璃控件的快照和 canvas 会显式使用 ARGB8888。壁纸已编译进 `src/img.c`，玻璃演示不依赖 LVGL 文件系统驱动。

## 内存与绘制开销

ARGB8888 每像素占 4 字节。当前演示分辨率为 320 x 483，单个全屏快照约占 604 KiB，此外每个玻璃控件还需要自己的 canvas 缓冲区。移植到更大屏幕或增加较多、大尺寸玻璃控件时，应相应增加 LVGL 内存池。默认渲染器使用 CPU，模糊和折射的开销会随玻璃控件的数量及尺寸增长。
