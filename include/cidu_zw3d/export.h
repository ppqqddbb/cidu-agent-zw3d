#pragma once

// 总结：根据当前平台和编译目标，统一定义插件导出/导入符号。
#if defined(_WIN32)
#if defined(CIDU_ZW3D_PLUGIN_BUILD)
#define CIDU_ZW3D_API __declspec(dllexport)
#else
#define CIDU_ZW3D_API __declspec(dllimport)
#endif
#else
#define CIDU_ZW3D_API
#endif
