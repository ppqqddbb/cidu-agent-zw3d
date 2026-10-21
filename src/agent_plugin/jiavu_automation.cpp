#include "jiavu_automation.h"

#include "cidu_zw3d/plugin_config.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <thread>
#include <limits>
#include <vector>

#if defined(_WIN32)
#include <UIAutomation.h>
#include <combaseapi.h>
#include <oleauto.h>
#include <windows.h>
#endif

namespace cidu_zw3d {

namespace {

std::wstring trim(std::wstring value) {
    while (!value.empty() && std::iswspace(value.front())) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::iswspace(value.back())) {
        value.pop_back();
    }
    return value;
}

std::wstring base_template_name(std::wstring value) {
    value = trim(value);
    const std::wstring suffix = L".jyasm";
    if (value.size() >= suffix.size()) {
        std::wstring tail = value.substr(value.size() - suffix.size());
        std::transform(tail.begin(), tail.end(), tail.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        if (tail == suffix) {
            value.resize(value.size() - suffix.size());
        }
    }
    return value;
}

std::wstring clean_model_text(std::wstring value) {
    value = trim(value);
    const std::vector<std::wstring> prefixes = {
        L"模型ID：", L"模型ID:", L"模板ID：", L"模板ID:",
        L"模板名称：", L"模板名称:", L"模板：", L"模板:",
        L"模型：", L"模型:", L"名称：", L"名称:", L"名：", L"名:"};
    bool changed = true;
    while (changed) {
        changed = false;
        value = trim(value);
        for (const auto& prefix : prefixes) {
            if (value.rfind(prefix, 0) == 0) {
                value = value.substr(prefix.size());
                changed = true;
                break;
            }
        }
    }
    return trim(value);
}

std::wstring normalize_key(const std::wstring& value) {
    std::wstring result;
    for (wchar_t ch : base_template_name(clean_model_text(value))) {
        if (std::iswspace(ch) || ch == L',' || ch == L'，' || ch == L'。' ||
            ch == L'；' || ch == L';' || ch == L':' || ch == L'：' ||
            ch == L'、' || ch == L'(' || ch == L')' || ch == L'（' ||
            ch == L'）' || ch == L'_' || ch == L'-' || ch == L'*' ||
            ch == L'`') {
            continue;
        }
        result.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    return result;
}

bool contains_key(const std::wstring& haystack, const std::wstring& needle) {
    const std::wstring lhs = normalize_key(haystack);
    const std::wstring rhs = normalize_key(needle);
    return !lhs.empty() && !rhs.empty() && lhs.find(rhs) != std::wstring::npos;
}

std::string utf8_text(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
#if defined(_WIN32)
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string text(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, text.data(), size, nullptr, nullptr);
    return text;
#else
    return std::string(value.begin(), value.end());
#endif
}

void debug_log(const std::wstring& message) {
    std::ofstream log(module_directory() / L"cidu_agent_debug.log", std::ios::app);
    if (log) {
        log << utf8_text(L"jiavu_automation: " + message) << '\n';
    }
}

std::vector<std::wstring> split_path(const std::wstring& value) {
    std::vector<std::wstring> parts;
    std::wstring current;
    for (wchar_t ch : value) {
        if (ch == L'/' || ch == L'\\' || ch == L'>' || ch == L'＞') {
            const std::wstring item = trim(current);
            if (!item.empty()) {
                parts.push_back(item);
            }
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    const std::wstring item = trim(current);
    if (!item.empty()) {
        parts.push_back(item);
    }
    return parts;
}

std::vector<std::wstring> split_lines(const std::wstring& value) {
    std::vector<std::wstring> lines;
    std::wstring current;
    for (wchar_t ch : value) {
        if (ch == L'\r') {
            continue;
        }
        if (ch == L'\n') {
            lines.push_back(trim(current));
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    lines.push_back(trim(current));
    lines.erase(
        std::remove_if(lines.begin(), lines.end(), [](const std::wstring& line) {
            return line.empty();
        }),
        lines.end());
    return lines;
}

#if defined(_WIN32)

template <typename T>
void release_ptr(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

struct UiElement {
    IUIAutomationElement* value = nullptr;

    UiElement() = default;
    explicit UiElement(IUIAutomationElement* element) : value(element) {}
    UiElement(const UiElement&) = delete;
    UiElement& operator=(const UiElement&) = delete;
    UiElement(UiElement&& other) noexcept : value(other.value) {
        other.value = nullptr;
    }
    UiElement& operator=(UiElement&& other) noexcept {
        if (this != &other) {
            release_ptr(value);
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }
    ~UiElement() {
        release_ptr(value);
    }
    explicit operator bool() const {
        return value != nullptr;
    }
};

struct UiAutomation {
    IUIAutomation* value = nullptr;
    bool com_initialized = false;

    UiAutomation() {
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        com_initialized = SUCCEEDED(init);
        if (init == RPC_E_CHANGED_MODE) {
            com_initialized = false;
        }
        CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&value));
    }
    UiAutomation(const UiAutomation&) = delete;
    UiAutomation& operator=(const UiAutomation&) = delete;
    ~UiAutomation() {
        release_ptr(value);
        if (com_initialized) {
            CoUninitialize();
        }
    }
    explicit operator bool() const {
        return value != nullptr;
    }
};

struct ParameterApplyResult {
    int requested = 0;
    int applied = 0;
    int displayed = 0;
    bool backend_only = false;
    bool page_refresh_confirmed = false;
};

struct OrderedParameter {
    std::wstring name;
    std::wstring value;
};

struct QtEditorCandidate {
    void* widget = nullptr;
    int x = 0;
    int y = 0;
    bool combo = false;
};

struct QtViewCandidate {
    void* view = nullptr;
    bool tree = false;
};

struct QtIndexCandidate {
    alignas(void*) unsigned char storage[64] = {};
    int y = 0;
    int global_x = 0;
    int global_y = 0;
    int row = -1;
    int column = -1;
};

struct JiavuDataParserCandidate {
    void* parser = nullptr;
    int count = 0;
    int score = 0;
};

struct JiavuDataPropCandidate {
    void* prop = nullptr;
    int score = 0;
    std::wstring name;
    std::wstring unit;
    std::wstring value;
    bool edit = false;
    bool combo = false;
    bool disabled = false;
};

RECT element_rect(IUIAutomationElement* element);
bool usable_rect(const RECT& rect);
bool visible_screen_rect(const RECT& rect);
int clamp_int(int value, int low, int high);
bool point_inside_rects(int x, int y, const std::vector<RECT>& rects);
void debug_log(const std::wstring& message);

struct QtDirectApi {
    struct Point {
        int x = 0;
        int y = 0;
    };

    using WidgetAtFn = void* (__fastcall*)(int, int);
    using InheritsFn = bool (__fastcall*)(const void*, const char*);
    using ParentWidgetFn = void* (__fastcall*)(void*);
    using SetLineEditTextFn = void (__fastcall*)(void*, const void*);
    using SetComboTextFn = void (__fastcall*)(void*, const void*);
    using SetComboEditTextFn = void (__fastcall*)(void*, const void*);
    using IsComboEditableFn = bool (__fastcall*)(const void*);
    using UpdateWidgetFn = void (__fastcall*)(void*);
    using FromWCharArrayFn = void* (__fastcall*)(void*, const wchar_t*, int);
    using QStringDtorFn = void (__fastcall*)(void*);
    using TableRowCountFn = int (__fastcall*)(const void*);
    using TableColumnCountFn = int (__fastcall*)(const void*);
    using TableCellWidgetFn = void* (__fastcall*)(const void*, int, int);
    using TableItemFn = void* (__fastcall*)(const void*, int, int);
    using TableItemSetTextFn = void (__fastcall*)(void*, const void*);
    using TableRowHeightFn = int (__fastcall*)(const void*, int);
    using TableColumnWidthFn = int (__fastcall*)(const void*, int);
    using TableRowViewportPositionFn = int (__fastcall*)(const void*, int);
    using TableColumnViewportPositionFn = int (__fastcall*)(const void*, int);
    using ViewportFn = void* (__fastcall*)(const void*);
    using MapToGlobalFn = Point (__fastcall*)(const void*, const Point*);
    using TableIndexAtFn = void* (__fastcall*)(void*, const void*, const Point*);
    using TreeColumnWidthFn = int (__fastcall*)(const void*, int);
    using TreeColumnViewportPositionFn = int (__fastcall*)(const void*, int);
    using TreeIndexAtFn = void* (__fastcall*)(void*, const void*, const Point*);
    using ModelIndexIsValidFn = bool (__fastcall*)(const void*);
    using ModelIndexRowFn = int (__fastcall*)(const void*);
    using ModelIndexColumnFn = int (__fastcall*)(const void*);
    using SetCurrentIndexFn = void (__fastcall*)(void*, const void*);
    using EditIndexFn = void (__fastcall*)(void*, const void*);
    using FocusWidgetFn = void* (__fastcall*)();
    using CommitDataFn = void (__fastcall*)(void*, void*);
    using CloseEditorFn = void (__fastcall*)(void*, void*, int);
    using MetaObjectFn = const void* (__fastcall*)(const void*);
    using ClassNameFn = const char* (__fastcall*)(const void*);
    using SuperClassFn = const void* (__fastcall*)(const void*);

    WidgetAtFn widget_at = nullptr;
    InheritsFn inherits = nullptr;
    ParentWidgetFn parent_widget = nullptr;
    SetLineEditTextFn set_line_edit_text = nullptr;
    SetComboTextFn set_combo_current_text = nullptr;
    SetComboEditTextFn set_combo_edit_text = nullptr;
    IsComboEditableFn is_combo_editable = nullptr;
    UpdateWidgetFn update_widget = nullptr;
    FromWCharArrayFn from_wchar_array = nullptr;
    QStringDtorFn destroy_qstring = nullptr;
    TableRowCountFn table_row_count = nullptr;
    TableColumnCountFn table_column_count = nullptr;
    TableCellWidgetFn table_cell_widget = nullptr;
    TableItemFn table_item = nullptr;
    TableItemSetTextFn table_item_set_text = nullptr;
    TableRowHeightFn table_row_height = nullptr;
    TableColumnWidthFn table_column_width = nullptr;
    TableRowViewportPositionFn table_row_viewport_position = nullptr;
    TableColumnViewportPositionFn table_column_viewport_position = nullptr;
    ViewportFn viewport = nullptr;
    MapToGlobalFn map_to_global = nullptr;
    TableIndexAtFn table_index_at = nullptr;
    TreeColumnWidthFn tree_column_width = nullptr;
    TreeColumnViewportPositionFn tree_column_viewport_position = nullptr;
    TreeIndexAtFn tree_index_at = nullptr;
    ModelIndexIsValidFn model_index_is_valid = nullptr;
    ModelIndexRowFn model_index_row = nullptr;
    ModelIndexColumnFn model_index_column = nullptr;
    SetCurrentIndexFn set_current_index = nullptr;
    EditIndexFn edit_index = nullptr;
    FocusWidgetFn focus_widget = nullptr;
    CommitDataFn commit_data = nullptr;
    CloseEditorFn close_editor = nullptr;
    MetaObjectFn meta_object = nullptr;
    ClassNameFn class_name = nullptr;
    SuperClassFn super_class = nullptr;

    bool ok() const {
        return widget_write_ok();
    }

    bool widget_write_ok() const {
        return widget_at && inherits && parent_widget && set_line_edit_text &&
               set_combo_current_text && set_combo_edit_text && is_combo_editable &&
               update_widget && from_wchar_array && destroy_qstring;
    }

    bool table_write_ok() const {
        return view_write_ok() &&
               table_row_height && table_column_width &&
               table_row_viewport_position && table_column_viewport_position &&
               table_index_at;
    }

    bool tree_view_write_ok() const {
        return view_write_ok() &&
               tree_column_width && tree_column_viewport_position && tree_index_at;
    }

    bool view_write_ok() const {
        return widget_write_ok() &&
               viewport && map_to_global &&
               model_index_is_valid && model_index_row && model_index_column &&
               set_current_index && edit_index && focus_widget &&
               commit_data && close_editor;
    }

    bool table_widget_write_ok() const {
        return table_write_ok() && table_row_count && table_column_count &&
               table_cell_widget && table_item && table_item_set_text;
    }

    bool diagnostics_ok() const {
        return widget_at && parent_widget && meta_object && class_name && super_class;
    }
};

struct JiavuDataParserApi {
    using GetCountFn = int (__fastcall*)(const void*);
    using SetValueFn = int (__fastcall*)(void*, int, const std::wstring&);
    using GetValueFn = void* (__fastcall*)(void*, const void*, int);
    using GetItemPropExFn = void* (__fastcall*)(void*, const void*, int);
    using GetPropTextRefFn = const std::wstring& (__fastcall*)(const void*);
    using GetPropBoolFn = bool (__fastcall*)(const void*);
    using SetPropValFn = void (__fastcall*)(void*, const std::wstring&);
    using ParsePropValueFn = int (__fastcall*)(void*);
    using IsValidPropValueFn = bool (__fastcall*)(void*, const std::wstring&);
    using ParseValueFn = int (__fastcall*)(void*);

    HMODULE module = nullptr;
    GetCountFn get_count = nullptr;
    SetValueFn set_value = nullptr;
    GetValueFn get_value = nullptr;
    GetItemPropExFn get_item_prop_ex = nullptr;
    GetPropTextRefFn prop_name = nullptr;
    GetPropTextRefFn prop_val = nullptr;
    GetPropTextRefFn prop_unit = nullptr;
    GetPropTextRefFn prop_value_exp = nullptr;
    GetPropBoolFn prop_is_edit = nullptr;
    GetPropBoolFn prop_is_combo = nullptr;
    GetPropBoolFn prop_is_disabled = nullptr;
    GetPropBoolFn prop_is_modify = nullptr;
    SetPropValFn prop_set_val = nullptr;
    SetPropValFn prop_set_value_exp = nullptr;
    ParsePropValueFn prop_parse_value = nullptr;
    IsValidPropValueFn prop_is_valid_value = nullptr;
    ParseValueFn parse_value = nullptr;
    void* parser_vtable = nullptr;
    void* prop_vtable = nullptr;

    bool ok() const {
        return module && get_count && set_value && get_value && parse_value &&
               get_item_prop_ex && prop_name && prop_val && parser_vtable &&
               prop_set_val && prop_vtable;
    }

    bool prop_ok() const {
        return module && prop_name && prop_val && prop_set_val && prop_vtable;
    }
};

JiavuDataParserApi jiavu_data_parser_api() {
    JiavuDataParserApi api;
    api.module = GetModuleHandleW(L"DataParser.dll");
    if (!api.module) {
        return api;
    }
    api.get_count = reinterpret_cast<JiavuDataParserApi::GetCountFn>(
        GetProcAddress(api.module, "?GetItemPropCount@CDataParser@@QEBAHXZ"));
    api.set_value = reinterpret_cast<JiavuDataParserApi::SetValueFn>(
        GetProcAddress(api.module, "?SetItemPropValue@CDataParser@@QEAAHHAEBV?$basic_string@_WU?$char_traits@_W@std@@V?$allocator@_W@2@@std@@@Z"));
    api.get_value = reinterpret_cast<JiavuDataParserApi::GetValueFn>(
        GetProcAddress(api.module, "?GetItemPropValue@CDataParser@@QEBA?AV?$basic_string@_WU?$char_traits@_W@std@@V?$allocator@_W@2@@std@@H@Z"));
    api.get_item_prop_ex = reinterpret_cast<JiavuDataParserApi::GetItemPropExFn>(
        GetProcAddress(api.module, "?GetItemPropEx@CDataParser@@QEBA?BV?$shared_ptr@$$CBVCDataItemProp@@@std@@H@Z"));
    api.prop_name = reinterpret_cast<JiavuDataParserApi::GetPropTextRefFn>(
        GetProcAddress(api.module, "?GetName@CDataItemProp@@QEBAAEBV?$basic_string@_WU?$char_traits@_W@std@@V?$allocator@_W@2@@std@@XZ"));
    api.prop_val = reinterpret_cast<JiavuDataParserApi::GetPropTextRefFn>(
        GetProcAddress(api.module, "?GetVal@CDataItemProp@@QEBAAEBV?$basic_string@_WU?$char_traits@_W@std@@V?$allocator@_W@2@@std@@XZ"));
    api.prop_unit = reinterpret_cast<JiavuDataParserApi::GetPropTextRefFn>(
        GetProcAddress(api.module, "?GetUnit@CDataItemProp@@QEBAAEBV?$basic_string@_WU?$char_traits@_W@std@@V?$allocator@_W@2@@std@@XZ"));
    api.prop_value_exp = reinterpret_cast<JiavuDataParserApi::GetPropTextRefFn>(
        GetProcAddress(api.module, "?GetValueExp@CDataItemProp@@QEBAAEBV?$basic_string@_WU?$char_traits@_W@std@@V?$allocator@_W@2@@std@@XZ"));
    api.prop_is_edit = reinterpret_cast<JiavuDataParserApi::GetPropBoolFn>(
        GetProcAddress(api.module, "?IsEdit@CDataItemProp@@QEBA_NXZ"));
    api.prop_is_combo = reinterpret_cast<JiavuDataParserApi::GetPropBoolFn>(
        GetProcAddress(api.module, "?IsCombo@CDataItemProp@@QEBA_NXZ"));
    api.prop_is_disabled = reinterpret_cast<JiavuDataParserApi::GetPropBoolFn>(
        GetProcAddress(api.module, "?IsDisabled@CDataItemProp@@QEBA_NXZ"));
    api.prop_is_modify = reinterpret_cast<JiavuDataParserApi::GetPropBoolFn>(
        GetProcAddress(api.module, "?IsModify@CDataItemProp@@QEBA_NXZ"));
    api.prop_set_val = reinterpret_cast<JiavuDataParserApi::SetPropValFn>(
        GetProcAddress(api.module, "?SetVal@CDataItemProp@@QEAAXAEBV?$basic_string@_WU?$char_traits@_W@std@@V?$allocator@_W@2@@std@@@Z"));
    api.prop_set_value_exp = reinterpret_cast<JiavuDataParserApi::SetPropValFn>(
        GetProcAddress(api.module, "?SetValueExp@CDataItemProp@@QEAAXAEBV?$basic_string@_WU?$char_traits@_W@std@@V?$allocator@_W@2@@std@@@Z"));
    api.prop_parse_value = reinterpret_cast<JiavuDataParserApi::ParsePropValueFn>(
        GetProcAddress(api.module, "?ParseValue@CDataItemProp@@QEAAHXZ"));
    api.prop_is_valid_value = reinterpret_cast<JiavuDataParserApi::IsValidPropValueFn>(
        GetProcAddress(api.module, "?IsValidValue@CDataItemProp@@QEAA_NAEBV?$basic_string@_WU?$char_traits@_W@std@@V?$allocator@_W@2@@std@@@Z"));
    api.parse_value = reinterpret_cast<JiavuDataParserApi::ParseValueFn>(
        GetProcAddress(api.module, "?ParseValue@CDataParser@@QEAAHXZ"));
    auto* parser_vtable_symbol = reinterpret_cast<std::uintptr_t*>(
        GetProcAddress(api.module, "??_7CDataParser@@6B@"));
    if (parser_vtable_symbol) {
        api.parser_vtable = reinterpret_cast<void*>(parser_vtable_symbol);
    }
    auto* prop_vtable_symbol = reinterpret_cast<std::uintptr_t*>(
        GetProcAddress(api.module, "??_7CDataItemProp@@6B@"));
    if (prop_vtable_symbol) {
        api.prop_vtable = reinterpret_cast<void*>(prop_vtable_symbol);
    }
    return api;
}

bool readable_memory(const void* pointer, std::size_t size) {
    if (!pointer || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(pointer, &info, sizeof(info)) != sizeof(info)) {
        return false;
    }
    if (info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) ||
        (info.Protect & PAGE_NOACCESS)) {
        return false;
    }
    const auto start = reinterpret_cast<std::uintptr_t>(pointer);
    const auto region_start = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    const auto region_end = region_start + info.RegionSize;
    return start >= region_start && size <= region_end - start;
}

bool likely_heap_pointer(void* pointer) {
    if (!readable_memory(pointer, sizeof(void*))) {
        return false;
    }
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(pointer, &info, sizeof(info)) != sizeof(info)) {
        return false;
    }
    // Jiavu can keep some page state in DLL-owned static storage.  Earlier
    // scans only accepted MEM_PRIVATE heap objects, which misses those cases.
    if (info.Type != MEM_PRIVATE && info.Type != MEM_IMAGE) {
        return false;
    }
    return (info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE |
                            PAGE_EXECUTE_WRITECOPY)) != 0;
}

bool looks_like_data_parser_object(const JiavuDataParserApi& api, void* pointer) {
    if (!api.parser_vtable || !likely_heap_pointer(pointer)) {
        return false;
    }
    void* vtable = nullptr;
    std::memcpy(&vtable, pointer, sizeof(vtable));
    return vtable == api.parser_vtable;
}

bool looks_like_data_prop_object(const JiavuDataParserApi& api, const void* pointer) {
    if (!api.prop_vtable || !likely_heap_pointer(const_cast<void*>(pointer))) {
        return false;
    }
    void* vtable = nullptr;
    std::memcpy(&vtable, pointer, sizeof(vtable));
    return vtable == api.prop_vtable;
}

struct StdWStringReturn {
    const JiavuDataParserApi& api;
    alignas(void*) unsigned char storage[64] = {};
    bool constructed = false;

    explicit StdWStringReturn(const JiavuDataParserApi& parser_api) : api(parser_api) {}
    StdWStringReturn(const StdWStringReturn&) = delete;
    StdWStringReturn& operator=(const StdWStringReturn&) = delete;
    ~StdWStringReturn() {
        (void)api;
        if (constructed) {
            reinterpret_cast<std::wstring*>(storage)->~basic_string();
        }
    }

    std::wstring value() const {
        const auto* text = reinterpret_cast<const std::wstring*>(storage);
        return *text;
    }
};

struct StdSharedPtrReturn {
    alignas(void*) unsigned char storage[32] = {};
    bool constructed = false;

    StdSharedPtrReturn() = default;
    StdSharedPtrReturn(const StdSharedPtrReturn&) = delete;
    StdSharedPtrReturn& operator=(const StdSharedPtrReturn&) = delete;

    ~StdSharedPtrReturn() {
        if (constructed) {
            reinterpret_cast<std::shared_ptr<const void>*>(storage)->~shared_ptr();
        }
    }

    const void* get() const {
        if (!constructed) {
            return nullptr;
        }
        return reinterpret_cast<const std::shared_ptr<const void>*>(storage)->get();
    }
};

bool safe_data_parser_count(const JiavuDataParserApi& api, void* parser, int& count) {
    count = 0;
    if (!api.get_count || !looks_like_data_parser_object(api, parser)) {
        return false;
    }
    __try {
        count = api.get_count(parser);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return count > 0 && count < 512;
}

bool call_data_parser_get_value(
    JiavuDataParserApi::GetValueFn getter,
    void* storage,
    void* parser,
    int index) {
    __try {
        getter(storage, parser, index);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool call_data_parser_get_prop(
    JiavuDataParserApi::GetItemPropExFn getter,
    void* storage,
    void* parser,
    int index) {
    __try {
        getter(storage, parser, index);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool call_data_prop_text_ref(
    JiavuDataParserApi::GetPropTextRefFn getter,
    const void* prop,
    const std::wstring** value) {
    *value = nullptr;
    __try {
        *value = &getter(prop);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *value = nullptr;
        return false;
    }
}

bool read_data_prop_text_view(
    const std::wstring* value,
    const wchar_t** data,
    std::size_t& size) {
    *data = nullptr;
    size = 0;
    if (!value) {
        return false;
    }
    __try {
        size = value->size();
        if (size > 4096) {
            return false;
        }
        *data = value->c_str();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *data = nullptr;
        size = 0;
        return false;
    }
}

bool copy_data_prop_text_chars(const wchar_t* data, wchar_t* output, std::size_t size) {
    if (size == 0) {
        return true;
    }
    if (!data || !output) {
        return false;
    }
    __try {
        std::memcpy(output, data, size * sizeof(wchar_t));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool call_data_prop_bool(
    JiavuDataParserApi::GetPropBoolFn getter,
    const void* prop,
    bool& value) {
    value = false;
    __try {
        value = getter(prop);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        value = false;
        return false;
    }
}

bool call_data_parser_set_value(
    JiavuDataParserApi::SetValueFn setter,
    void* parser,
    int index,
    const std::wstring& value,
    int& code) {
    code = -1;
    __try {
        code = setter(parser, index, value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        code = -1;
        return false;
    }
}

bool call_data_parser_parse_value(
    JiavuDataParserApi::ParseValueFn parser_fn,
    void* parser,
    int& code) {
    code = -1;
    __try {
        code = parser_fn(parser);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        code = -1;
        return false;
    }
}

bool call_data_prop_set_val(
    JiavuDataParserApi::SetPropValFn setter,
    void* prop,
    const std::wstring& value) {
    __try {
        setter(prop, value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool call_data_prop_parse_value(
    JiavuDataParserApi::ParsePropValueFn parser_fn,
    void* prop,
    int& code) {
    code = -1;
    __try {
        code = parser_fn(prop);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        code = -1;
        return false;
    }
}

bool call_data_prop_is_valid_value(
    JiavuDataParserApi::IsValidPropValueFn validator,
    void* prop,
    const std::wstring& value,
    bool& valid) {
    valid = false;
    __try {
        valid = validator(prop, value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        valid = false;
        return false;
    }
}

bool scan_region_for_parser_vtable(
    unsigned char* region,
    unsigned char* next,
    void* parser_vtable,
    void** output,
    std::size_t output_capacity,
    std::size_t& output_count) {
    output_count = 0;
    if (!region || !next || !parser_vtable || !output || output_capacity == 0) {
        return false;
    }
    __try {
        for (auto* cursor = region;
             cursor + sizeof(void*) <= next && output_count < output_capacity;
             cursor += sizeof(void*)) {
            void* value = nullptr;
            std::memcpy(&value, cursor, sizeof(value));
            if (value == parser_vtable) {
                output[output_count++] = cursor;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        output_count = 0;
        return false;
    }
}

bool safe_data_parser_set_value(
    const JiavuDataParserApi& api,
    void* parser,
    int index,
    const std::wstring& value);
bool safe_data_parser_parse_value(const JiavuDataParserApi& api, void* parser);
bool safe_data_prop_set_val(const JiavuDataParserApi& api, void* prop, const std::wstring& value);
bool safe_data_prop_set_value_exp(const JiavuDataParserApi& api, void* prop, const std::wstring& value);
bool safe_data_prop_parse_value(const JiavuDataParserApi& api, void* prop);
bool safe_data_prop_is_valid_value(const JiavuDataParserApi& api, void* prop, const std::wstring& value);
UiElement find_edit_right_of_label(UiAutomation& automation, IUIAutomationElement* root, IUIAutomationElement* label);
UiElement find_value_control_from_multiline_table(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const std::vector<std::wstring>& names,
    const std::vector<RECT>& excluded_rects,
    bool allow_activate);
UiElement find_parameter_label(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const std::vector<std::wstring>& names,
    const std::vector<RECT>& excluded_rects);

std::wstring safe_data_parser_value(const JiavuDataParserApi& api, void* parser, int index) {
    if (!api.get_value || !looks_like_data_parser_object(api, parser)) {
        return L"";
    }
    StdWStringReturn out(api);
    if (!call_data_parser_get_value(api.get_value, out.storage, parser, index)) {
        return L"";
    }
    out.constructed = true;
    return out.value();
}

const void* safe_data_parser_prop(
    const JiavuDataParserApi& api,
    void* parser,
    int index,
    StdSharedPtrReturn& out) {
    if (!api.get_item_prop_ex || !looks_like_data_parser_object(api, parser)) {
        return nullptr;
    }
    if (!call_data_parser_get_prop(api.get_item_prop_ex, out.storage, parser, index)) {
        return nullptr;
    }
    out.constructed = true;
    return out.get();
}

std::wstring safe_data_prop_text(
    const JiavuDataParserApi& api,
    const void* prop,
    JiavuDataParserApi::GetPropTextRefFn getter) {
    if (!getter || !prop || !looks_like_data_prop_object(api, prop)) {
        return L"";
    }
    const std::wstring* value = nullptr;
    if (!call_data_prop_text_ref(getter, prop, &value) || !value) {
        return L"";
    }
    const wchar_t* data = nullptr;
    std::size_t size = 0;
    if (!read_data_prop_text_view(value, &data, size)) {
        return L"";
    }
    std::wstring output(size, L'\0');
    if (!copy_data_prop_text_chars(data, output.data(), size)) {
        return L"";
    }
    return output;
}

bool safe_data_prop_bool(
    const JiavuDataParserApi& api,
    const void* prop,
    JiavuDataParserApi::GetPropBoolFn getter) {
    if (!getter || !prop || !looks_like_data_prop_object(api, prop)) {
        return false;
    }
    bool value = false;
    if (!call_data_prop_bool(getter, prop, value)) {
        return false;
    }
    return value;
}

std::wstring bracketed_parameter_key(const std::wstring& name, const std::wstring& unit) {
    std::wstring result = trim(name);
    const std::wstring clean_unit = trim(unit);
    if (!result.empty() && !clean_unit.empty() &&
        result.find(L'[') == std::wstring::npos &&
        result.find(L'【') == std::wstring::npos) {
        result += L"[" + clean_unit + L"]";
    }
    return result;
}

bool parameter_names_match(
    const std::wstring& requested,
    const std::wstring& prop_name,
    const std::wstring& prop_unit) {
    const std::wstring req = normalize_key(requested);
    const std::wstring name = normalize_key(prop_name);
    const std::wstring with_unit = normalize_key(bracketed_parameter_key(prop_name, prop_unit));
    return !req.empty() && (!name.empty() || !with_unit.empty()) &&
           (req == name || req == with_unit || name.find(req) != std::wstring::npos ||
            req.find(name) != std::wstring::npos || with_unit.find(req) != std::wstring::npos ||
           req.find(with_unit) != std::wstring::npos);
}

bool parameter_names_exact_match(
    const std::wstring& requested,
    const std::wstring& prop_name,
    const std::wstring& prop_unit) {
    const std::wstring req = normalize_key(requested);
    const std::wstring name = normalize_key(prop_name);
    const std::wstring with_unit = normalize_key(bracketed_parameter_key(prop_name, prop_unit));
    return !req.empty() && (req == name || req == with_unit);
}

int writable_parameter_count(const std::vector<OrderedParameter>& parameters) {
    int count = 0;
    for (const auto& parameter : parameters) {
        if (!trim(parameter.value).empty() && !contains_key(parameter.name, L"关键词")) {
            ++count;
        }
    }
    return count;
}

int data_prop_score_for_parameters(
    const JiavuDataParserApi& api,
    const void* prop,
    const std::vector<OrderedParameter>& parameters,
    std::wstring& name,
    std::wstring& unit,
    std::wstring& value,
    bool& edit,
    bool& combo,
    bool& disabled) {
    name = safe_data_prop_text(api, prop, api.prop_name);
    unit = safe_data_prop_text(api, prop, api.prop_unit);
    value = safe_data_prop_text(api, prop, api.prop_val);
    edit = safe_data_prop_bool(api, prop, api.prop_is_edit);
    combo = safe_data_prop_bool(api, prop, api.prop_is_combo);
    disabled = safe_data_prop_bool(api, prop, api.prop_is_disabled);

    if (trim(name).empty()) {
        return 0;
    }
    int score = disabled ? -20 : 2;
    if (edit || combo) {
        score += 8;
    }
    for (const auto& parameter : parameters) {
        if (parameter_names_exact_match(parameter.name, name, unit)) {
            score += 100;
            break;
        }
        if (parameter_names_match(parameter.name, name, unit)) {
            score += 20;
            break;
        }
    }
    return score;
}

std::vector<JiavuDataPropCandidate> find_data_prop_candidates(
    const JiavuDataParserApi& api,
    const std::vector<OrderedParameter>& parameters) {
    std::vector<JiavuDataPropCandidate> candidates;
    if (!api.prop_ok()) {
        debug_log(
            L"DataProp API unavailable module=" +
            std::to_wstring(reinterpret_cast<std::uintptr_t>(api.module)) +
            L" name=" + std::to_wstring(reinterpret_cast<std::uintptr_t>(api.prop_name)) +
            L" val=" + std::to_wstring(reinterpret_cast<std::uintptr_t>(api.prop_val)) +
            L" set=" + std::to_wstring(reinterpret_cast<std::uintptr_t>(api.prop_set_val)) +
            L" vtable=" + std::to_wstring(reinterpret_cast<std::uintptr_t>(api.prop_vtable)));
        return candidates;
    }

    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    auto* address = static_cast<unsigned char*>(system_info.lpMinimumApplicationAddress);
    auto* max_address = static_cast<unsigned char*>(system_info.lpMaximumApplicationAddress);
    int scanned_regions = 0;
    int found_props = 0;
    int named_props = 0;
    int logged_samples = 0;

    while (address < max_address && scanned_regions < 8192) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info)) {
            break;
        }
        auto* region = static_cast<unsigned char*>(info.BaseAddress);
        auto* next = region + info.RegionSize;
        const bool readable =
            info.State == MEM_COMMIT &&
            (info.Type == MEM_PRIVATE || info.Type == MEM_IMAGE) &&
            !(info.Protect & PAGE_GUARD) &&
            !(info.Protect & PAGE_NOACCESS) &&
            (info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE |
                             PAGE_EXECUTE_WRITECOPY));
        if (readable && info.RegionSize >= sizeof(void*) && info.RegionSize <= 128 * 1024 * 1024) {
            ++scanned_regions;
            void* found[128] = {};
            std::size_t found_count = 0;
            if (!scan_region_for_parser_vtable(
                    region,
                    next,
                    api.prop_vtable,
                    found,
                    sizeof(found) / sizeof(found[0]),
                    found_count)) {
                debug_log(L"DataProp scan skipped unreadable changed region");
            }
            for (std::size_t i = 0; i < found_count; ++i) {
                void* prop = found[i];
                ++found_props;
                if (!looks_like_data_prop_object(api, prop)) {
                    continue;
                }
                const bool exists = std::any_of(
                    candidates.begin(),
                    candidates.end(),
                    [&](const JiavuDataPropCandidate& item) {
                        return item.prop == prop;
                    });
                if (exists) {
                    continue;
                }
                std::wstring name;
                std::wstring unit;
                std::wstring value;
                bool edit = false;
                bool combo = false;
                bool disabled = false;
                const int score = data_prop_score_for_parameters(
                    api,
                    prop,
                    parameters,
                    name,
                    unit,
                    value,
                    edit,
                    combo,
                    disabled);
                if (trim(name).empty()) {
                    continue;
                }
                ++named_props;
                if (score >= 100 || logged_samples < 40) {
                    debug_log(
                        L"DataProp candidate prop=0x" +
                        std::to_wstring(reinterpret_cast<std::uintptr_t>(prop)) +
                        L" score=" + std::to_wstring(score) +
                        L" name=" + bracketed_parameter_key(name, unit) +
                        L" val=" + value +
                        L" edit=" + std::to_wstring(edit ? 1 : 0) +
                        L" combo=" + std::to_wstring(combo ? 1 : 0) +
                        L" disabled=" + std::to_wstring(disabled ? 1 : 0));
                    ++logged_samples;
                }
                if (score >= 100 && candidates.size() < 512) {
                    candidates.push_back({prop, score, name, unit, value, edit, combo, disabled});
                }
            }
        }
        if (next <= address) {
            break;
        }
        address = next;
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        return bracketed_parameter_key(lhs.name, lhs.unit) < bracketed_parameter_key(rhs.name, rhs.unit);
    });
    debug_log(
        L"DataProp scan summary candidates=" + std::to_wstring(candidates.size()) +
        L" found=" + std::to_wstring(found_props) +
        L" named=" + std::to_wstring(named_props) +
        L" regions=" + std::to_wstring(scanned_regions));
    return candidates;
}

ParameterApplyResult apply_parameters_by_data_item_props(
    const std::vector<OrderedParameter>& parameters) {
    ParameterApplyResult result;
    result.requested = writable_parameter_count(parameters);
    result.backend_only = true;
    if (result.requested == 0) {
        return result;
    }

    const JiavuDataParserApi api = jiavu_data_parser_api();
    std::vector<JiavuDataPropCandidate> candidates = find_data_prop_candidates(api, parameters);
    if (candidates.empty()) {
        debug_log(L"DataProp write: no matched parameter objects");
        return result;
    }

    for (const auto& parameter : parameters) {
        if (trim(parameter.value).empty() || contains_key(parameter.name, L"关键词")) {
            continue;
        }
        int written_objects = 0;
        int skipped_disabled = 0;
        for (const auto& candidate : candidates) {
            if (!parameter_names_match(parameter.name, candidate.name, candidate.unit)) {
                continue;
            }
            if (candidate.disabled) {
                ++skipped_disabled;
                continue;
            }
            if (!parameter_names_exact_match(parameter.name, candidate.name, candidate.unit)) {
                continue;
            }
            const bool valid_value = safe_data_prop_is_valid_value(api, candidate.prop, parameter.value);
            debug_log(
                L"DataProp value check parameter=" + parameter.name +
                L" prop=" + bracketed_parameter_key(candidate.name, candidate.unit) +
                L" value=" + parameter.value +
                L" valid=" + std::to_wstring(valid_value ? 1 : 0));
            if (!safe_data_prop_set_val(api, candidate.prop, parameter.value)) {
                debug_log(
                    L"DataProp write failed parameter=" + parameter.name +
                    L" prop=" + bracketed_parameter_key(candidate.name, candidate.unit));
                continue;
            }
            safe_data_prop_set_value_exp(api, candidate.prop, parameter.value);
            ++written_objects;
            safe_data_prop_parse_value(api, candidate.prop);
            const std::wstring after_value = safe_data_prop_text(api, candidate.prop, api.prop_val);
            const std::wstring after_exp = safe_data_prop_text(api, candidate.prop, api.prop_value_exp);
            const bool modified = safe_data_prop_bool(api, candidate.prop, api.prop_is_modify);
            debug_log(
                L"DataProp write parameter=" + parameter.name +
                L" prop=" + bracketed_parameter_key(candidate.name, candidate.unit) +
                L" old=" + candidate.value +
                L" value=" + parameter.value +
                L" after=" + after_value +
                L" exp=" + after_exp +
                L" modify=" + std::to_wstring(modified ? 1 : 0));
            if (written_objects >= 16) {
                break;
            }
        }
        if (written_objects > 0) {
            ++result.applied;
        } else {
            debug_log(
                L"DataProp write: parameter not written=" + parameter.name +
                L" disabled_matches=" + std::to_wstring(skipped_disabled));
        }
    }
    return result;
}

int data_parser_score_for_parameters(
    const JiavuDataParserApi& api,
    void* parser,
    const std::vector<OrderedParameter>& parameters,
    int count) {
    int score = 0;
    const int max_rows = (std::min)(count, 160);
    for (int index = 0; index < max_rows; ++index) {
        StdSharedPtrReturn prop_holder;
        const void* prop = safe_data_parser_prop(api, parser, index, prop_holder);
        if (!prop) {
            continue;
        }
        const std::wstring name = safe_data_prop_text(api, prop, api.prop_name);
        const std::wstring unit = safe_data_prop_text(api, prop, api.prop_unit);
        const std::wstring val = safe_data_prop_text(api, prop, api.prop_val);
        if (!trim(name).empty()) {
            score += 1;
        }
        if (safe_data_prop_bool(api, prop, api.prop_is_edit) ||
            safe_data_prop_bool(api, prop, api.prop_is_combo)) {
            score += 1;
        }
        for (const auto& parameter : parameters) {
            if (parameter_names_match(parameter.name, name, unit)) {
                score += 20;
                debug_log(
                    L"DataParser candidate matched index=" + std::to_wstring(index) +
                    L" requested=" + parameter.name +
                    L" prop=" + bracketed_parameter_key(name, unit) +
                    L" val=" + val);
                break;
            }
        }
    }
    return score;
}

void debug_data_parser_sample(
    const JiavuDataParserApi& api,
    void* parser,
    int count,
    int score) {
    debug_log(
        L"DataParser candidate parser=0x" +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(parser)) +
        L" count=" + std::to_wstring(count) +
        L" score=" + std::to_wstring(score));
    const int max_rows = (std::min)(count, 12);
    for (int index = 0; index < max_rows; ++index) {
        StdSharedPtrReturn prop_holder;
        const void* prop = safe_data_parser_prop(api, parser, index, prop_holder);
        if (!prop) {
            continue;
        }
        const std::wstring name = safe_data_prop_text(api, prop, api.prop_name);
        const std::wstring unit = safe_data_prop_text(api, prop, api.prop_unit);
        const std::wstring val = safe_data_prop_text(api, prop, api.prop_val);
        if (trim(name).empty() && trim(val).empty()) {
            continue;
        }
        debug_log(
            L"DataParser prop index=" + std::to_wstring(index) +
            L" name=" + bracketed_parameter_key(name, unit) +
            L" val=" + val +
            L" edit=" + std::to_wstring(safe_data_prop_bool(api, prop, api.prop_is_edit) ? 1 : 0) +
            L" combo=" + std::to_wstring(safe_data_prop_bool(api, prop, api.prop_is_combo) ? 1 : 0) +
            L" disabled=" + std::to_wstring(safe_data_prop_bool(api, prop, api.prop_is_disabled) ? 1 : 0));
    }
}

std::vector<JiavuDataParserCandidate> find_data_parser_candidates(
    const JiavuDataParserApi& api,
    const std::vector<OrderedParameter>& parameters) {
    std::vector<JiavuDataParserCandidate> candidates;
    if (!api.ok()) {
        debug_log(L"DataParser API unavailable");
        return candidates;
    }

    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    auto* address = static_cast<unsigned char*>(system_info.lpMinimumApplicationAddress);
    auto* max_address = static_cast<unsigned char*>(system_info.lpMaximumApplicationAddress);
    int scanned_regions = 0;
    while (address < max_address && scanned_regions < 8192) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info)) {
            break;
        }
        auto* region = static_cast<unsigned char*>(info.BaseAddress);
        auto* next = region + info.RegionSize;
        const bool readable =
            info.State == MEM_COMMIT &&
            (info.Type == MEM_PRIVATE || info.Type == MEM_IMAGE) &&
            !(info.Protect & PAGE_GUARD) &&
            !(info.Protect & PAGE_NOACCESS) &&
            (info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE |
                             PAGE_EXECUTE_WRITECOPY));
        if (readable && info.RegionSize >= sizeof(void*) && info.RegionSize <= 128 * 1024 * 1024) {
            ++scanned_regions;
            void* found[64] = {};
            std::size_t found_count = 0;
            if (!scan_region_for_parser_vtable(
                    region,
                    next,
                    api.parser_vtable,
                    found,
                    (std::min)(std::size_t{64}, std::size_t{48} - candidates.size()),
                    found_count)) {
                debug_log(L"DataParser scan skipped unreadable changed region");
            }
            for (std::size_t i = 0; i < found_count && candidates.size() < 48; ++i) {
                void* parser = found[i];
                const bool exists = std::any_of(
                    candidates.begin(),
                    candidates.end(),
                    [&](const JiavuDataParserCandidate& item) {
                        return item.parser == parser;
                    });
                if (exists) {
                    continue;
                }
                int count = 0;
                if (!safe_data_parser_count(api, parser, count)) {
                    continue;
                }
                const int score = data_parser_score_for_parameters(api, parser, parameters, count);
                candidates.push_back({parser, count, score});
                debug_data_parser_sample(api, parser, count, score);
            }
        }
        if (next <= address) {
            break;
        }
        address = next;
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        return lhs.count > rhs.count;
    });
    debug_log(
        L"DataParser scan summary candidates=" + std::to_wstring(candidates.size()) +
        L" regions=" + std::to_wstring(scanned_regions));
    return candidates;
}

ParameterApplyResult apply_parameters_by_data_parser(
    const std::vector<OrderedParameter>& parameters) {
    ParameterApplyResult result;
    result.requested = writable_parameter_count(parameters);
    result.backend_only = true;
    if (parameters.empty()) {
        return result;
    }
    const JiavuDataParserApi api = jiavu_data_parser_api();
    std::vector<JiavuDataParserCandidate> candidates = find_data_parser_candidates(api, parameters);
    if (candidates.empty() || candidates.front().score <= 0) {
        debug_log(L"DataParser write: no matched parser candidate");
        return result;
    }

    void* parser = candidates.front().parser;
    const int count = candidates.front().count;
    for (const auto& parameter : parameters) {
        if (trim(parameter.value).empty()) {
            continue;
        }
        int matched_index = -1;
        for (int index = 0; index < count; ++index) {
            StdSharedPtrReturn prop_holder;
            const void* prop = safe_data_parser_prop(api, parser, index, prop_holder);
            if (!prop) {
                continue;
            }
            const std::wstring name = safe_data_prop_text(api, prop, api.prop_name);
            const std::wstring unit = safe_data_prop_text(api, prop, api.prop_unit);
            if (parameter_names_match(parameter.name, name, unit)) {
                matched_index = index;
                break;
            }
        }
        if (matched_index < 0) {
            debug_log(L"DataParser write: parameter not found=" + parameter.name);
            continue;
        }
        if (safe_data_parser_set_value(api, parser, matched_index, parameter.value)) {
            ++result.applied;
            debug_log(
                L"DataParser write parameter=" + parameter.name +
                L" index=" + std::to_wstring(matched_index) +
                L" value=" + parameter.value);
            StdSharedPtrReturn prop_holder;
            const void* prop = safe_data_parser_prop(api, parser, matched_index, prop_holder);
            if (prop) {
                const std::wstring after_value = safe_data_prop_text(api, prop, api.prop_val);
                const std::wstring after_exp = safe_data_prop_text(api, prop, api.prop_value_exp);
                debug_log(
                    L"DataParser after parameter=" + parameter.name +
                    L" index=" + std::to_wstring(matched_index) +
                    L" after=" + after_value +
                    L" exp=" + after_exp);
            }
        }
    }
    if (result.applied > 0) {
        safe_data_parser_parse_value(api, parser);
    }
    return result;
}

bool safe_data_parser_set_value(
    const JiavuDataParserApi& api,
    void* parser,
    int index,
    const std::wstring& value) {
    if (!api.set_value || !looks_like_data_parser_object(api, parser) || trim(value).empty()) {
        return false;
    }
    int code = -1;
    if (!call_data_parser_set_value(api.set_value, parser, index, value, code)) {
        return false;
    }
    debug_log(
        L"DataParser SetItemPropValue index=" + std::to_wstring(index) +
        L" code=" + std::to_wstring(code));
    return code == 0;
}

bool safe_data_parser_parse_value(const JiavuDataParserApi& api, void* parser) {
    if (!api.parse_value || !looks_like_data_parser_object(api, parser)) {
        return false;
    }
    int code = -1;
    if (!call_data_parser_parse_value(api.parse_value, parser, code)) {
        return false;
    }
    debug_log(L"DataParser ParseValue code=" + std::to_wstring(code));
    return code == 0;
}

bool safe_data_prop_set_val(
    const JiavuDataParserApi& api,
    void* prop,
    const std::wstring& value) {
    if (!api.prop_set_val || !looks_like_data_prop_object(api, prop) || trim(value).empty()) {
        return false;
    }
    const bool ok = call_data_prop_set_val(api.prop_set_val, prop, value);
    debug_log(
        L"DataProp SetVal prop=0x" +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(prop)) +
        L" ok=" + std::to_wstring(ok ? 1 : 0));
    return ok;
}

bool safe_data_prop_set_value_exp(
    const JiavuDataParserApi& api,
    void* prop,
    const std::wstring& value) {
    if (!api.prop_set_value_exp || !looks_like_data_prop_object(api, prop) || trim(value).empty()) {
        return false;
    }
    const bool ok = call_data_prop_set_val(api.prop_set_value_exp, prop, value);
    debug_log(
        L"DataProp SetValueExp prop=0x" +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(prop)) +
        L" ok=" + std::to_wstring(ok ? 1 : 0));
    return ok;
}

bool safe_data_prop_parse_value(const JiavuDataParserApi& api, void* prop) {
    if (!api.prop_parse_value || !looks_like_data_prop_object(api, prop)) {
        return false;
    }
    int code = -1;
    if (!call_data_prop_parse_value(api.prop_parse_value, prop, code)) {
        debug_log(
            L"DataProp ParseValue prop=0x" +
            std::to_wstring(reinterpret_cast<std::uintptr_t>(prop)) +
            L" exception");
        return false;
    }
    debug_log(
        L"DataProp ParseValue prop=0x" +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(prop)) +
        L" code=" + std::to_wstring(code));
    return code == 0;
}

bool safe_data_prop_is_valid_value(
    const JiavuDataParserApi& api,
    void* prop,
    const std::wstring& value) {
    if (!api.prop_is_valid_value || !looks_like_data_prop_object(api, prop)) {
        return true;
    }
    bool valid = false;
    if (!call_data_prop_is_valid_value(api.prop_is_valid_value, prop, value, valid)) {
        debug_log(
            L"DataProp IsValidValue prop=0x" +
            std::to_wstring(reinterpret_cast<std::uintptr_t>(prop)) +
            L" exception");
        return true;
    }
    return valid;
}

QtDirectApi qt_direct_api() {
    QtDirectApi api;
    HMODULE widgets = GetModuleHandleW(L"Qt5Widgets.dll");
    HMODULE core = GetModuleHandleW(L"Qt5Core.dll");
    if (!widgets || !core) {
        return api;
    }
    api.widget_at = reinterpret_cast<QtDirectApi::WidgetAtFn>(
        GetProcAddress(widgets, "?widgetAt@QApplication@@SAPEAVQWidget@@HH@Z"));
    api.inherits = reinterpret_cast<QtDirectApi::InheritsFn>(
        GetProcAddress(core, "?inherits@QObject@@QEBA_NPEBD@Z"));
    api.parent_widget = reinterpret_cast<QtDirectApi::ParentWidgetFn>(
        GetProcAddress(widgets, "?parentWidget@QWidget@@QEBAPEAV1@XZ"));
    api.set_line_edit_text = reinterpret_cast<QtDirectApi::SetLineEditTextFn>(
        GetProcAddress(widgets, "?setText@QLineEdit@@QEAAXAEBVQString@@@Z"));
    api.set_combo_current_text = reinterpret_cast<QtDirectApi::SetComboTextFn>(
        GetProcAddress(widgets, "?setCurrentText@QComboBox@@QEAAXAEBVQString@@@Z"));
    api.set_combo_edit_text = reinterpret_cast<QtDirectApi::SetComboEditTextFn>(
        GetProcAddress(widgets, "?setEditText@QComboBox@@QEAAXAEBVQString@@@Z"));
    api.is_combo_editable = reinterpret_cast<QtDirectApi::IsComboEditableFn>(
        GetProcAddress(widgets, "?isEditable@QComboBox@@QEBA_NXZ"));
    api.update_widget = reinterpret_cast<QtDirectApi::UpdateWidgetFn>(
        GetProcAddress(widgets, "?update@QWidget@@QEAAXXZ"));
    api.from_wchar_array = reinterpret_cast<QtDirectApi::FromWCharArrayFn>(
        GetProcAddress(core, "?fromWCharArray@QString@@SA?AV1@PEB_WH@Z"));
    api.destroy_qstring = reinterpret_cast<QtDirectApi::QStringDtorFn>(
        GetProcAddress(core, "??1QString@@QEAA@XZ"));
    api.table_row_count = reinterpret_cast<QtDirectApi::TableRowCountFn>(
        GetProcAddress(widgets, "?rowCount@QTableWidget@@QEBAHXZ"));
    api.table_column_count = reinterpret_cast<QtDirectApi::TableColumnCountFn>(
        GetProcAddress(widgets, "?columnCount@QTableWidget@@QEBAHXZ"));
    api.table_cell_widget = reinterpret_cast<QtDirectApi::TableCellWidgetFn>(
        GetProcAddress(widgets, "?cellWidget@QTableWidget@@QEBAPEAVQWidget@@HH@Z"));
    api.table_item = reinterpret_cast<QtDirectApi::TableItemFn>(
        GetProcAddress(widgets, "?item@QTableWidget@@QEBAPEAVQTableWidgetItem@@HH@Z"));
    api.table_item_set_text = reinterpret_cast<QtDirectApi::TableItemSetTextFn>(
        GetProcAddress(widgets, "?setText@QTableWidgetItem@@QEAAXAEBVQString@@@Z"));
    api.table_row_height = reinterpret_cast<QtDirectApi::TableRowHeightFn>(
        GetProcAddress(widgets, "?rowHeight@QTableView@@QEBAHH@Z"));
    api.table_column_width = reinterpret_cast<QtDirectApi::TableColumnWidthFn>(
        GetProcAddress(widgets, "?columnWidth@QTableView@@QEBAHH@Z"));
    api.table_row_viewport_position = reinterpret_cast<QtDirectApi::TableRowViewportPositionFn>(
        GetProcAddress(widgets, "?rowViewportPosition@QTableView@@QEBAHH@Z"));
    api.table_column_viewport_position = reinterpret_cast<QtDirectApi::TableColumnViewportPositionFn>(
        GetProcAddress(widgets, "?columnViewportPosition@QTableView@@QEBAHH@Z"));
    api.viewport = reinterpret_cast<QtDirectApi::ViewportFn>(
        GetProcAddress(widgets, "?viewport@QAbstractScrollArea@@QEBAPEAVQWidget@@XZ"));
    api.map_to_global = reinterpret_cast<QtDirectApi::MapToGlobalFn>(
        GetProcAddress(widgets, "?mapToGlobal@QWidget@@QEBA?AVQPoint@@AEBV2@@Z"));
    api.table_index_at = reinterpret_cast<QtDirectApi::TableIndexAtFn>(
        GetProcAddress(widgets, "?indexAt@QTableView@@UEBA?AVQModelIndex@@AEBVQPoint@@@Z"));
    api.tree_column_width = reinterpret_cast<QtDirectApi::TreeColumnWidthFn>(
        GetProcAddress(widgets, "?columnWidth@QTreeView@@QEBAHH@Z"));
    api.tree_column_viewport_position = reinterpret_cast<QtDirectApi::TreeColumnViewportPositionFn>(
        GetProcAddress(widgets, "?columnViewportPosition@QTreeView@@QEBAHH@Z"));
    api.tree_index_at = reinterpret_cast<QtDirectApi::TreeIndexAtFn>(
        GetProcAddress(widgets, "?indexAt@QTreeView@@UEBA?AVQModelIndex@@AEBVQPoint@@@Z"));
    api.model_index_is_valid = reinterpret_cast<QtDirectApi::ModelIndexIsValidFn>(
        GetProcAddress(core, "?isValid@QModelIndex@@QEBA_NXZ"));
    api.model_index_row = reinterpret_cast<QtDirectApi::ModelIndexRowFn>(
        GetProcAddress(core, "?row@QModelIndex@@QEBAHXZ"));
    api.model_index_column = reinterpret_cast<QtDirectApi::ModelIndexColumnFn>(
        GetProcAddress(core, "?column@QModelIndex@@QEBAHXZ"));
    api.set_current_index = reinterpret_cast<QtDirectApi::SetCurrentIndexFn>(
        GetProcAddress(widgets, "?setCurrentIndex@QAbstractItemView@@QEAAXAEBVQModelIndex@@@Z"));
    api.edit_index = reinterpret_cast<QtDirectApi::EditIndexFn>(
        GetProcAddress(widgets, "?edit@QAbstractItemView@@QEAAXAEBVQModelIndex@@@Z"));
    api.focus_widget = reinterpret_cast<QtDirectApi::FocusWidgetFn>(
        GetProcAddress(widgets, "?focusWidget@QApplication@@SAPEAVQWidget@@XZ"));
    api.commit_data = reinterpret_cast<QtDirectApi::CommitDataFn>(
        GetProcAddress(widgets, "?commitData@QAbstractItemView@@MEAAXPEAVQWidget@@@Z"));
    api.close_editor = reinterpret_cast<QtDirectApi::CloseEditorFn>(
        GetProcAddress(widgets, "?closeEditor@QAbstractItemView@@MEAAXPEAVQWidget@@W4EndEditHint@QAbstractItemDelegate@@@Z"));
    api.meta_object = reinterpret_cast<QtDirectApi::MetaObjectFn>(
        GetProcAddress(core, "?metaObject@QObject@@UEBAPEBUQMetaObject@@XZ"));
    api.class_name = reinterpret_cast<QtDirectApi::ClassNameFn>(
        GetProcAddress(core, "?className@QMetaObject@@QEBAPEBDXZ"));
    api.super_class = reinterpret_cast<QtDirectApi::SuperClassFn>(
        GetProcAddress(core, "?superClass@QMetaObject@@QEBAPEBU1@XZ"));
    return api;
}

struct QtStringValue {
    const QtDirectApi& api;
    alignas(void*) unsigned char storage[32] = {};
    bool valid = false;

    QtStringValue(const QtDirectApi& qt, const std::wstring& value) : api(qt) {
        if (api.from_wchar_array) {
            api.from_wchar_array(storage, value.c_str(), static_cast<int>(value.size()));
            valid = true;
        }
    }

    QtStringValue(const QtStringValue&) = delete;
    QtStringValue& operator=(const QtStringValue&) = delete;

    ~QtStringValue() {
        if (valid && api.destroy_qstring) {
            api.destroy_qstring(storage);
        }
    }

    const void* data() const {
        return valid ? storage : nullptr;
    }
};

void* qt_parent_of_type(const QtDirectApi& api, void* widget, const char* class_name) {
    for (int depth = 0; widget && depth < 20; ++depth) {
        if (api.inherits(widget, class_name)) {
            return widget;
        }
        widget = api.parent_widget(widget);
    }
    return nullptr;
}

std::wstring ascii_to_wide(const char* value) {
    std::wstring result;
    if (!value) {
        return result;
    }
    while (*value) {
        result.push_back(static_cast<unsigned char>(*value));
        ++value;
    }
    return result;
}

std::wstring qt_meta_class_chain(const QtDirectApi& api, void* widget) {
    if (!api.diagnostics_ok() || !widget) {
        return L"";
    }
    const void* meta = api.meta_object(widget);
    std::wstring chain;
    for (int depth = 0; meta && depth < 16; ++depth) {
        const char* raw_name = api.class_name(meta);
        if (!raw_name || !*raw_name) {
            break;
        }
        if (!chain.empty()) {
            chain += L" <- ";
        }
        chain += ascii_to_wide(raw_name);
        meta = api.super_class(meta);
    }
    return chain;
}

std::wstring qt_known_type_flags(const QtDirectApi& api, void* widget) {
    if (!api.inherits || !widget) {
        return L"";
    }
    const std::vector<const char*> types = {
        "QWidget",
        "QFrame",
        "QAbstractScrollArea",
        "QAbstractItemView",
        "QTableView",
        "QTableWidget",
        "QTreeView",
        "QListView",
        "QLineEdit",
        "QComboBox",
        "QTextEdit",
        "QPlainTextEdit",
        "QSpinBox",
        "QDoubleSpinBox",
        "QAbstractButton",
        "QPushButton",
        "QTabWidget",
        "QTabBar",
        "QStackedWidget",
        "QDockWidget",
        "QQuickWidget",
        "QWebEngineView"
    };
    std::wstring flags;
    for (const char* type : types) {
        if (!api.inherits(widget, type)) {
            continue;
        }
        if (!flags.empty()) {
            flags += L",";
        }
        flags += ascii_to_wide(type);
    }
    return flags;
}

void debug_qt_widget_surface(
    const QtDirectApi& api,
    const RECT& root_rect,
    const std::vector<RECT>& excluded_rects) {
    if (!api.diagnostics_ok() || !visible_screen_rect(root_rect)) {
        debug_log(L"Qt diagnostics unavailable");
        return;
    }

    std::vector<void*> seen_widgets;
    std::vector<std::wstring> seen_chains;
    const int width = root_rect.right - root_rect.left;
    const int height = root_rect.bottom - root_rect.top;
    const int step_x = (std::max)(24, width / 12);
    const int step_y = (std::max)(24, height / 18);
    int logged = 0;
    for (int y = root_rect.top + 6; y < root_rect.bottom - 6 && logged < 80; y += step_y) {
        for (int x = root_rect.left + 6; x < root_rect.right - 6 && logged < 80; x += step_x) {
            if (point_inside_rects(x, y, excluded_rects)) {
                continue;
            }
            void* widget = api.widget_at(x, y);
            if (!widget || std::find(seen_widgets.begin(), seen_widgets.end(), widget) != seen_widgets.end()) {
                continue;
            }
            seen_widgets.push_back(widget);
            std::wstring chain = qt_meta_class_chain(api, widget);
            if (chain.empty()) {
                continue;
            }
            const bool new_chain = std::find(seen_chains.begin(), seen_chains.end(), chain) == seen_chains.end();
            if (new_chain) {
                seen_chains.push_back(chain);
            }
            debug_log(
                L"Qt widget probe point=" + std::to_wstring(x) +
                L"," + std::to_wstring(y) +
                L" new_class=" + std::to_wstring(new_chain ? 1 : 0) +
                L" chain=" + chain +
                L" inherits=" + qt_known_type_flags(api, widget));

            void* parent = api.parent_widget(widget);
            for (int depth = 1; parent && depth <= 8; ++depth) {
                std::wstring parent_chain = qt_meta_class_chain(api, parent);
                if (!parent_chain.empty()) {
                    debug_log(
                        L"Qt widget parent depth=" + std::to_wstring(depth) +
                        L" chain=" + parent_chain +
                        L" inherits=" + qt_known_type_flags(api, parent));
                }
                parent = api.parent_widget(parent);
            }
            ++logged;
        }
    }
    debug_log(
        L"Qt widget probe summary widgets=" + std::to_wstring(seen_widgets.size()) +
        L" unique_chains=" + std::to_wstring(seen_chains.size()));
}

bool set_qt_widget_value(const QtDirectApi& api, void* widget, const std::wstring& value) {
    if (!api.widget_write_ok() || !widget || trim(value).empty()) {
        return false;
    }

    QtStringValue text(api, value);
    if (!text.data()) {
        return false;
    }

    if (void* combo = qt_parent_of_type(api, widget, "QComboBox")) {
        if (api.is_combo_editable(combo)) {
            api.set_combo_edit_text(combo, text.data());
        } else {
            api.set_combo_current_text(combo, text.data());
        }
        api.update_widget(combo);
        debug_log(L"set Qt combo value=" + value);
        return true;
    }

    if (void* line_edit = qt_parent_of_type(api, widget, "QLineEdit")) {
        api.set_line_edit_text(line_edit, text.data());
        api.update_widget(line_edit);
        debug_log(L"set Qt line edit value=" + value);
        return true;
    }

    return false;
}

bool set_qt_table_item_text(
    const QtDirectApi& api,
    void* table,
    int row,
    int column,
    const std::wstring& value) {
    if (!api.table_item || !api.table_item_set_text || !api.from_wchar_array ||
        !api.destroy_qstring || !table || trim(value).empty()) {
        return false;
    }

    void* item = api.table_item(table, row, column);
    if (!item) {
        return false;
    }

    QtStringValue text(api, value);
    if (!text.data()) {
        return false;
    }
    api.table_item_set_text(item, text.data());
    api.update_widget(table);
    debug_log(
        L"set Qt table item row=" + std::to_wstring(row) +
        L" column=" + std::to_wstring(column) +
        L" value=" + value);
    return true;
}

bool edit_qt_view_index(
    const QtDirectApi& api,
    void* view,
    void* fallback_widget,
    const void* index_storage,
    const std::wstring& value) {
    if (!api.view_write_ok() || !view || !index_storage || trim(value).empty()) {
        return false;
    }

    if (!api.model_index_is_valid(index_storage)) {
        return false;
    }

    api.set_current_index(view, index_storage);
    api.edit_index(view, index_storage);
    void* editor = api.focus_widget();
    if (!editor) {
        editor = fallback_widget;
    }
    if (!set_qt_widget_value(api, editor, value)) {
        debug_log(
            L"Qt view editor write failed row=" + std::to_wstring(api.model_index_row(index_storage)) +
            L" column=" + std::to_wstring(api.model_index_column(index_storage)));
        return false;
    }
    api.commit_data(view, editor);
    api.close_editor(view, editor, 3);
    api.update_widget(view);
    debug_log(
        L"set Qt view editor row=" + std::to_wstring(api.model_index_row(index_storage)) +
        L" column=" + std::to_wstring(api.model_index_column(index_storage)) +
        L" value=" + value);
    return true;
}

bool edit_qt_table_cell_by_geometry(
    const QtDirectApi& api,
    void* table,
    int row,
    int column,
    const std::wstring& value) {
    if (!api.table_write_ok() || !table || trim(value).empty()) {
        return false;
    }

    void* viewport = api.viewport(table);
    if (!viewport) {
        return false;
    }
    const int row_height = api.table_row_height(table, row);
    const int column_width = api.table_column_width(table, column);
    if (row_height <= 0 || column_width <= 0) {
        return false;
    }

    alignas(void*) unsigned char index_storage[64] = {};
    QtDirectApi::Point local = {
        api.table_column_viewport_position(table, column) + column_width / 2,
        api.table_row_viewport_position(table, row) + row_height / 2
    };
    api.table_index_at(index_storage, table, &local);
    if (!api.model_index_is_valid(index_storage)) {
        debug_log(
            L"Qt table index invalid row=" + std::to_wstring(row) +
            L" column=" + std::to_wstring(column));
        return false;
    }

    const QtDirectApi::Point global = api.map_to_global(viewport, &local);
    void* fallback_widget = api.widget_at(global.x, global.y);
    return edit_qt_view_index(api, table, fallback_widget, index_storage, value);
}

bool set_qt_table_widget_cell_value(
    const QtDirectApi& api,
    void* table,
    int row,
    int column,
    const std::wstring& value) {
    if (!api.table_widget_write_ok() || !table || trim(value).empty()) {
        return false;
    }
    if (void* cell = api.table_cell_widget(table, row, column)) {
        if (set_qt_widget_value(api, cell, value)) {
            debug_log(
                L"set Qt table cell widget direct row=" + std::to_wstring(row) +
                L" column=" + std::to_wstring(column));
            return true;
        }
    }
    return set_qt_table_item_text(api, table, row, column, value);
}

bool set_qt_table_cell_value(
    const QtDirectApi& api,
    void* table,
    int row,
    int column,
    const std::wstring& value) {
    if (api.table_widget_write_ok() && set_qt_table_widget_cell_value(api, table, row, column, value)) {
        return true;
    }
    return edit_qt_table_cell_by_geometry(api, table, row, column, value);
}

std::vector<void*> qt_table_candidates_in_rect(
    const QtDirectApi& api,
    const RECT& rect,
    const std::vector<RECT>& excluded_rects);

bool same_qt_widget(const QtDirectApi& api, void* lhs, void* rhs) {
    if (!lhs || !rhs) {
        return false;
    }
    void* left_edit = qt_parent_of_type(api, lhs, "QLineEdit");
    void* right_edit = qt_parent_of_type(api, rhs, "QLineEdit");
    if (left_edit && right_edit && left_edit == right_edit) {
        return true;
    }
    void* left_combo = qt_parent_of_type(api, lhs, "QComboBox");
    void* right_combo = qt_parent_of_type(api, rhs, "QComboBox");
    return left_combo && right_combo && left_combo == right_combo;
}

std::vector<QtEditorCandidate> qt_editor_candidates_in_rect(
    const QtDirectApi& api,
    const RECT& rect,
    const std::vector<RECT>& excluded_rects) {
    std::vector<QtEditorCandidate> editors;
    if (!api.widget_write_ok() || !visible_screen_rect(rect)) {
        return editors;
    }

    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int step_x = (std::max)(8, width / 120);
    const int step_y = (std::max)(6, height / 150);
    for (int y = rect.top + 4; y < rect.bottom - 4; y += step_y) {
        for (int x = rect.left + 4; x < rect.right - 4; x += step_x) {
            if (point_inside_rects(x, y, excluded_rects)) {
                continue;
            }
            void* widget = api.widget_at(x, y);
            if (!widget) {
                continue;
            }
            void* combo = qt_parent_of_type(api, widget, "QComboBox");
            void* line_edit = combo ? nullptr : qt_parent_of_type(api, widget, "QLineEdit");
            void* editor = combo ? combo : line_edit;
            if (!editor) {
                continue;
            }
            const bool exists = std::any_of(editors.begin(), editors.end(), [&](const QtEditorCandidate& item) {
                return same_qt_widget(api, item.widget, editor);
            });
            if (!exists) {
                editors.push_back({editor, x, y, combo != nullptr});
                debug_log(
                    std::wstring(L"Qt editor candidate ") +
                    (combo ? L"combo" : L"line") +
                    L" at=" + std::to_wstring(x) +
                    L"," + std::to_wstring(y));
            }
        }
    }

    std::sort(editors.begin(), editors.end(), [](const QtEditorCandidate& lhs, const QtEditorCandidate& rhs) {
        if (std::abs(lhs.y - rhs.y) > 8) {
            return lhs.y < rhs.y;
        }
        return lhs.x < rhs.x;
    });
    return editors;
}

ParameterApplyResult apply_parameters_by_qt_editors(
    const QtDirectApi& api,
    const RECT& root_rect,
    const std::vector<OrderedParameter>& parameters,
    const std::vector<RECT>& excluded_rects) {
    ParameterApplyResult result;
    if (!api.widget_write_ok() || parameters.empty() || !visible_screen_rect(root_rect)) {
        return result;
    }

    std::vector<QtEditorCandidate> editors = qt_editor_candidates_in_rect(api, root_rect, excluded_rects);
    if (editors.empty()) {
        debug_log(L"Qt editor write: no QLineEdit/QComboBox candidates");
        return result;
    }

    int start = 0;
    while (start < static_cast<int>(editors.size()) && editors[static_cast<std::size_t>(start)].y < root_rect.top + 80) {
        ++start;
    }
    const int available = static_cast<int>(editors.size()) - start;
    if (available < static_cast<int>((std::min)(std::size_t{3}, parameters.size()))) {
        debug_log(L"Qt editor write: insufficient editor candidates=" + std::to_wstring(editors.size()));
        return result;
    }

    const int max_count = (std::min)(available, static_cast<int>(parameters.size()));
    for (int index = 0; index < max_count; ++index) {
        const auto& parameter = parameters[static_cast<std::size_t>(index)];
        if (trim(parameter.value).empty()) {
            continue;
        }
        ++result.requested;
        const auto& editor = editors[static_cast<std::size_t>(start + index)];
        if (set_qt_widget_value(api, editor.widget, parameter.value)) {
            ++result.applied;
            debug_log(
                L"Qt editor write parameter=" + parameter.name +
                L" editor_y=" + std::to_wstring(editor.y));
        } else {
            debug_log(
                L"Qt editor write failed parameter=" + parameter.name +
                L" editor_y=" + std::to_wstring(editor.y));
        }
    }
    return result;
}

int qt_table_score_for_parameters(const QtDirectApi& api, void* table, int requested_count) {
    if (!api.table_write_ok() || !table) {
        return -1;
    }
    int score = 0;
    if (api.inherits(table, "QTableWidget")) {
        score += 12;
    }
    if (api.inherits(table, "QTableView")) {
        score += 5;
    }
    for (int row = 0; row < requested_count && row < 80; ++row) {
        const int height = api.table_row_height(table, row);
        if (height > 0 && height <= 80) {
            score += 2;
        }
    }
    for (int column = 0; column < 4; ++column) {
        const int width = api.table_column_width(table, column);
        if (width > 20 && width <= 800) {
            score += column == 1 ? 5 : 1;
        }
    }
    return score;
}

std::vector<OrderedParameter> ordered_parameters_from_request(const JiavuAutomationRequest& request) {
    std::vector<OrderedParameter> ordered;
    auto add = [&](const std::wstring& name) {
        const auto found = request.parameters.find(name);
        if (found == request.parameters.end() || trim(found->second).empty() || contains_key(found->first, L"关键词")) {
            return;
        }
        const std::wstring normalized = normalize_key(found->first);
        const bool exists = std::any_of(ordered.begin(), ordered.end(), [&](const OrderedParameter& item) {
            return normalize_key(item.name) == normalized;
        });
        if (!exists) {
            ordered.push_back({found->first, found->second});
        }
    };

    for (const auto& name : request.parameter_order) {
        add(name);
    }
    for (const auto& [name, value] : request.parameters) {
        (void)value;
        add(name);
    }
    return ordered;
}

ParameterApplyResult apply_parameters_by_qt_table(
    const QtDirectApi& api,
    const RECT& root_rect,
    const std::vector<OrderedParameter>& parameters,
    const std::vector<RECT>& excluded_rects) {
    ParameterApplyResult result;
    if (!api.table_write_ok() || parameters.empty() || !visible_screen_rect(root_rect)) {
        return result;
    }

    std::vector<void*> tables = qt_table_candidates_in_rect(api, root_rect, excluded_rects);
    if (tables.empty()) {
        debug_log(L"Qt table write: no QTableWidget candidates");
        return result;
    }

    void* best_table = nullptr;
    int best_score = (std::numeric_limits<int>::min)();
    for (void* table : tables) {
        const int score = qt_table_score_for_parameters(api, table, static_cast<int>(parameters.size()));
        debug_log(L"Qt table score=" + std::to_wstring(score));
        if (score > best_score) {
            best_score = score;
            best_table = table;
        }
    }
    if (!best_table) {
        return result;
    }

    const int value_column = 1;
    debug_log(
        L"Qt table write: use value_column=" + std::to_wstring(value_column));

    for (int row = 0; row < static_cast<int>(parameters.size()); ++row) {
        const auto& parameter = parameters[static_cast<std::size_t>(row)];
        if (trim(parameter.value).empty()) {
            continue;
        }
        if (api.table_row_height(best_table, row) <= 0 || api.table_column_width(best_table, value_column) <= 0) {
            debug_log(L"Qt table write stop at invisible row=" + std::to_wstring(row));
            break;
        }
        ++result.requested;
        if (set_qt_table_cell_value(api, best_table, row, value_column, parameter.value)) {
            ++result.applied;
        } else {
            debug_log(
                L"Qt table write failed row=" + std::to_wstring(row) +
                L" parameter=" + parameter.name);
        }
    }
    return result;
}

std::vector<QtViewCandidate> qt_tree_candidates_in_rect(
    const QtDirectApi& api,
    const RECT& rect,
    const std::vector<RECT>& excluded_rects) {
    std::vector<QtViewCandidate> views;
    if (!api.tree_view_write_ok() || !visible_screen_rect(rect)) {
        return views;
    }

    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int step_x = (std::max)(12, width / 70);
    const int step_y = (std::max)(10, height / 90);
    for (int y = rect.top + 8; y < rect.bottom - 8; y += step_y) {
        for (int x = rect.left + 8; x < rect.right - 8; x += step_x) {
            if (point_inside_rects(x, y, excluded_rects)) {
                continue;
            }
            void* widget = api.widget_at(x, y);
            if (!widget) {
                continue;
            }
            void* tree = qt_parent_of_type(api, widget, "QTreeView");
            if (!tree) {
                continue;
            }
            const bool exists = std::any_of(views.begin(), views.end(), [&](const QtViewCandidate& item) {
                return item.view == tree;
            });
            if (exists) {
                continue;
            }
            views.push_back({tree, true});
            debug_log(
                L"Qt tree candidate hit=" + std::to_wstring(x) +
                L"," + std::to_wstring(y));
        }
    }
    return views;
}

std::vector<QtIndexCandidate> qt_tree_value_indexes(
    const QtDirectApi& api,
    void* tree,
    const RECT& root_rect,
    const std::vector<RECT>& excluded_rects) {
    std::vector<QtIndexCandidate> indexes;
    if (!api.tree_view_write_ok() || !tree) {
        return indexes;
    }
    void* viewport = api.viewport(tree);
    if (!viewport) {
        return indexes;
    }
    const int column = 1;
    const int column_width = api.tree_column_width(tree, column);
    if (column_width <= 0) {
        return indexes;
    }
    const int local_x = api.tree_column_viewport_position(tree, column) + column_width / 2;
    const int step_y = 6;
    const int viewport_height = root_rect.bottom - root_rect.top;
    for (int local_y = 0; local_y < viewport_height; local_y += step_y) {
        QtDirectApi::Point probe_local{local_x, local_y};
        QtDirectApi::Point global = api.map_to_global(viewport, &probe_local);
        if (!usable_rect(root_rect) || global.x < root_rect.left || global.x >= root_rect.right ||
            global.y < root_rect.top || global.y >= root_rect.bottom) {
            continue;
        }
        if (point_inside_rects(global.x, global.y, excluded_rects)) {
            continue;
        }
        alignas(void*) unsigned char index_storage[64] = {};
        api.tree_index_at(index_storage, tree, &probe_local);
        if (!api.model_index_is_valid(index_storage)) {
            continue;
        }
        const int row = api.model_index_row(index_storage);
        const int found_column = api.model_index_column(index_storage);
        if (found_column != column) {
            continue;
        }
        const bool exists = std::any_of(indexes.begin(), indexes.end(), [&](const QtIndexCandidate& item) {
            return item.row == row && item.column == found_column;
        });
        if (exists) {
            continue;
        }
        QtIndexCandidate candidate;
        std::memcpy(candidate.storage, index_storage, sizeof(candidate.storage));
        candidate.y = global.y;
        candidate.global_x = global.x;
        candidate.global_y = global.y;
        candidate.row = row;
        candidate.column = found_column;
        indexes.push_back(candidate);
        debug_log(
            L"Qt tree value index row=" + std::to_wstring(row) +
            L" column=" + std::to_wstring(found_column) +
            L" y=" + std::to_wstring(global.y));
    }

    std::sort(indexes.begin(), indexes.end(), [](const QtIndexCandidate& lhs, const QtIndexCandidate& rhs) {
        if (std::abs(lhs.y - rhs.y) > 4) {
            return lhs.y < rhs.y;
        }
        return lhs.row < rhs.row;
    });
    return indexes;
}

ParameterApplyResult apply_parameters_by_qt_tree(
    const QtDirectApi& api,
    const RECT& root_rect,
    const std::vector<OrderedParameter>& parameters,
    const std::vector<RECT>& excluded_rects) {
    ParameterApplyResult result;
    if (!api.tree_view_write_ok() || parameters.empty() || !visible_screen_rect(root_rect)) {
        return result;
    }

    std::vector<QtViewCandidate> trees = qt_tree_candidates_in_rect(api, root_rect, excluded_rects);
    if (trees.empty()) {
        debug_log(L"Qt tree write: no QTreeView candidates");
        return result;
    }

    void* best_tree = nullptr;
    std::vector<QtIndexCandidate> best_indexes;
    for (const auto& tree : trees) {
        std::vector<QtIndexCandidate> indexes = qt_tree_value_indexes(api, tree.view, root_rect, excluded_rects);
        debug_log(L"Qt tree index count=" + std::to_wstring(indexes.size()));
        if (indexes.size() > best_indexes.size()) {
            best_tree = tree.view;
            best_indexes = std::move(indexes);
        }
    }
    if (!best_tree || best_indexes.empty()) {
        return result;
    }

    std::size_t index_offset = 0;
    if (best_indexes.size() > parameters.size()) {
        index_offset = best_indexes.size() - parameters.size();
    }
    const std::size_t writable_count = (std::min)(parameters.size(), best_indexes.size() - index_offset);
    for (std::size_t i = 0; i < writable_count; ++i) {
        const auto& parameter = parameters[i];
        if (trim(parameter.value).empty()) {
            continue;
        }
        const QtIndexCandidate& index = best_indexes[index_offset + i];
        ++result.requested;
        void* fallback_widget = api.widget_at(index.global_x, index.global_y);
        if (edit_qt_view_index(api, best_tree, fallback_widget, index.storage, parameter.value)) {
            ++result.applied;
            debug_log(
                L"Qt tree write parameter=" + parameter.name +
                L" row=" + std::to_wstring(index.row));
        } else {
            debug_log(
                L"Qt tree write failed parameter=" + parameter.name +
                L" row=" + std::to_wstring(index.row));
        }
    }
    return result;
}

std::vector<void*> qt_table_candidates_in_rect(
    const QtDirectApi& api,
    const RECT& rect,
    const std::vector<RECT>& excluded_rects) {
    std::vector<void*> tables;
    if (!api.table_write_ok() || !visible_screen_rect(rect)) {
        return tables;
    }

    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int step_x = (std::max)(24, width / 44);
    const int step_y = (std::max)(18, height / 56);
    for (int y = rect.top + 8; y < rect.bottom - 8; y += step_y) {
        for (int x = rect.left + 8; x < rect.right - 8; x += step_x) {
            if (point_inside_rects(x, y, excluded_rects)) {
                continue;
            }
            void* widget = api.widget_at(x, y);
            if (!widget) {
                continue;
            }
            void* table = qt_parent_of_type(api, widget, "QTableView");
            if (!table) {
                continue;
            }
            if (std::find(tables.begin(), tables.end(), table) != tables.end()) {
                continue;
            }
            tables.push_back(table);
            debug_log(
                L"Qt table candidate hit=" + std::to_wstring(x) +
                L"," + std::to_wstring(y));
        }
    }
    return tables;
}

bool set_qt_value_right_of_label(
    const QtDirectApi& api,
    const RECT& root_rect,
    const RECT& label_rect,
    const std::wstring& value) {
    if (!api.ok() || !visible_screen_rect(root_rect) || !visible_screen_rect(label_rect)) {
        return false;
    }

    const int row_y = (label_rect.top + label_rect.bottom) / 2;
    const int min_x = (std::max)(label_rect.right + 12, root_rect.left + 12);
    const int max_x = root_rect.right - 18;
    if (max_x <= min_x) {
        return false;
    }

    std::vector<int> probes = {
        min_x + (max_x - min_x) / 8,
        min_x + (max_x - min_x) / 4,
        min_x + (max_x - min_x) / 2,
        min_x + ((max_x - min_x) * 3) / 4
    };
    probes.push_back((std::min)(static_cast<int>(label_rect.right + 80), max_x));
    probes.push_back((std::min)(static_cast<int>(label_rect.right + 150), max_x));

    for (int x : probes) {
        x = clamp_int(x, root_rect.left + 4, root_rect.right - 4);
        void* widget = api.widget_at(x, row_y);
        if (set_qt_widget_value(api, widget, value)) {
            debug_log(
                L"set Qt value by widgetAt point=" + std::to_wstring(x) +
                L"," + std::to_wstring(row_y));
            return true;
        }
    }
    return false;
}

std::wstring element_name(IUIAutomationElement* element) {
    if (!element) {
        return L"";
    }
    BSTR raw = nullptr;
    std::wstring name;
    if (SUCCEEDED(element->get_CurrentName(&raw)) && raw) {
        name = raw;
    }
    SysFreeString(raw);
    return name;
}

int element_control_type(IUIAutomationElement* element) {
    CONTROLTYPEID type = 0;
    if (element) {
        element->get_CurrentControlType(&type);
    }
    return type;
}

std::wstring control_type_text(int type) {
    switch (type) {
        case UIA_WindowControlTypeId:
            return L"Window";
        case UIA_PaneControlTypeId:
            return L"Pane";
        case UIA_TabItemControlTypeId:
            return L"TabItem";
        case UIA_ButtonControlTypeId:
            return L"Button";
        case UIA_TreeItemControlTypeId:
            return L"TreeItem";
        case UIA_ListItemControlTypeId:
            return L"ListItem";
        case UIA_DataItemControlTypeId:
            return L"DataItem";
        case UIA_EditControlTypeId:
            return L"Edit";
        case UIA_ComboBoxControlTypeId:
            return L"ComboBox";
        case UIA_TextControlTypeId:
            return L"Text";
        case UIA_CustomControlTypeId:
            return L"Custom";
        default:
            return L"Type" + std::to_wstring(type);
    }
}

RECT element_rect(IUIAutomationElement* element) {
    RECT rect = {};
    if (element) {
        element->get_CurrentBoundingRectangle(&rect);
    }
    return rect;
}

HWND element_native_window(IUIAutomationElement* element) {
    UIA_HWND raw = 0;
    if (element && SUCCEEDED(element->get_CurrentNativeWindowHandle(&raw)) && raw) {
        return reinterpret_cast<HWND>(raw);
    }
    return nullptr;
}

bool usable_rect(const RECT& rect) {
    return rect.right > rect.left && rect.bottom > rect.top;
}

int clamp_int(int value, int low, int high) {
    if (high < low) {
        return value;
    }
    return (std::max)(low, (std::min)(value, high));
}

bool visible_screen_rect(const RECT& rect) {
    if (!usable_rect(rect)) {
        return false;
    }
    const int virtual_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int virtual_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int virtual_right = virtual_left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int virtual_bottom = virtual_top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return rect.right > virtual_left && rect.left < virtual_right &&
           rect.bottom > virtual_top && rect.top < virtual_bottom;
}

bool onscreen_rect(const RECT& rect) {
    return usable_rect(rect) && rect.left >= 0 && rect.top >= 0 &&
           rect.right <= GetSystemMetrics(SM_CXSCREEN) &&
           rect.bottom <= GetSystemMetrics(SM_CYSCREEN);
}

std::wstring rect_text(const RECT& rect) {
    std::wostringstream out;
    out << L"[" << rect.left << L"," << rect.top << L"," << rect.right << L"," << rect.bottom << L"]";
    return out.str();
}

void debug_element(const std::wstring& prefix, IUIAutomationElement* element) {
    if (!element) {
        debug_log(prefix + L": <null>");
        return;
    }
    debug_log(
        prefix + L": name=" + element_name(element) +
        L" type=" + control_type_text(element_control_type(element)) +
        L" rect=" + rect_text(element_rect(element)));
}

void sleep_short(int milliseconds = 180) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

bool invoke_or_click(IUIAutomationElement* element);
UiElement find_first_descendant(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const std::function<bool(IUIAutomationElement*)>& predicate,
    int max_items);

bool click_element(IUIAutomationElement* element, bool double_click = false) {
    (void)double_click;
    debug_element(L"skip synthetic mouse input", element);
    return false;
}

bool is_clickable_control(IUIAutomationElement* element) {
    const int type = element_control_type(element);
    return type == UIA_ButtonControlTypeId ||
           type == UIA_TabItemControlTypeId ||
           type == UIA_TreeItemControlTypeId ||
           type == UIA_ListItemControlTypeId ||
           type == UIA_DataItemControlTypeId;
}

bool safe_click_element(IUIAutomationElement* element, bool double_click = false) {
    if (!element || !is_clickable_control(element)) {
        debug_element(L"skip unsafe click target", element);
        return false;
    }
    const RECT rect = element_rect(element);
    if (!usable_rect(rect)) {
        return false;
    }
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width > 900 || height > 140 || height < 8) {
        debug_element(L"skip suspicious click rect", element);
        return false;
    }
    debug_element(double_click ? L"safe double click" : L"safe click", element);
    return click_element(element, double_click);
}

bool select_item(IUIAutomationElement* element) {
    if (!element) {
        return false;
    }
    IUIAutomationSelectionItemPattern* selection = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selection))) && selection) {
        const HRESULT result = selection->Select();
        selection->Release();
        sleep_short(120);
        if (SUCCEEDED(result)) {
            debug_element(L"selected item", element);
            return true;
        }
    }
    return false;
}

bool set_combo_value(UiAutomation& automation, IUIAutomationElement* combo, const std::wstring& value) {
    if (!combo || trim(value).empty()) {
        return false;
    }

    HWND hwnd = element_native_window(combo);
    if (hwnd && IsWindow(hwnd)) {
        const std::wstring normalized_value = normalize_key(value);
        const LRESULT count = SendMessageW(hwnd, CB_GETCOUNT, 0, 0);
        if (count > 0 && count < 512) {
            for (int index = 0; index < static_cast<int>(count); ++index) {
                const LRESULT length = SendMessageW(hwnd, CB_GETLBTEXTLEN, index, 0);
                if (length <= 0 || length > 1024) {
                    continue;
                }
                std::wstring item(static_cast<std::size_t>(length), L'\0');
                if (SendMessageW(hwnd, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(item.data())) == CB_ERR) {
                    continue;
                }
                item.resize(wcslen(item.c_str()));
                if (normalize_key(item) == normalized_value || contains_key(item, value)) {
                    SendMessageW(hwnd, CB_SETCURSEL, index, 0);
                    HWND parent = GetParent(hwnd);
                    if (parent) {
                        SendMessageW(
                            parent,
                            WM_COMMAND,
                            MAKEWPARAM(GetDlgCtrlID(hwnd), CBN_SELCHANGE),
                            reinterpret_cast<LPARAM>(hwnd));
                    }
                    sleep_short(120);
                    debug_element(L"set combo value by native window", combo);
                    return true;
                }
            }
        }
    }

    IUIAutomationExpandCollapsePattern* expand = nullptr;
    if (SUCCEEDED(combo->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expand))) && expand) {
        expand->Expand();
        expand->Release();
        sleep_short(180);
    }

    UiElement option = find_first_descendant(
        automation,
        combo,
        [&](IUIAutomationElement* item) {
            const int type = element_control_type(item);
            if (type != UIA_ListItemControlTypeId && type != UIA_DataItemControlTypeId &&
                type != UIA_TextControlTypeId) {
                return false;
            }
            const std::wstring name = element_name(item);
            return normalize_key(name) == normalize_key(value) || contains_key(name, value);
        },
        1200);
    if (option && (select_item(option.value) || invoke_or_click(option.value))) {
        debug_element(L"set combo value", combo);
        return true;
    }

    IUIAutomationExpandCollapsePattern* collapse = nullptr;
    if (SUCCEEDED(combo->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&collapse))) && collapse) {
        collapse->Collapse();
        collapse->Release();
    }
    debug_element(L"failed set combo value", combo);
    return false;
}

bool focus_and_enter(IUIAutomationElement* element) {
    debug_element(L"skip synthetic keyboard input", element);
    return false;
}

bool invoke_or_click(IUIAutomationElement* element) {
    if (!element) {
        return false;
    }
    IUIAutomationInvokePattern* invoke = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke))) && invoke) {
        const HRESULT result = invoke->Invoke();
        invoke->Release();
        sleep_short();
        if (SUCCEEDED(result)) {
            debug_element(L"invoked", element);
            return true;
        }
    }
    if (select_item(element)) {
        return true;
    }
    return safe_click_element(element);
}

bool expand_or_click(IUIAutomationElement* element) {
    if (!element) {
        return false;
    }
    IUIAutomationExpandCollapsePattern* expand = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expand))) && expand) {
        ExpandCollapseState state = ExpandCollapseState_LeafNode;
        expand->get_CurrentExpandCollapseState(&state);
        if (state == ExpandCollapseState_Collapsed) {
            const HRESULT result = expand->Expand();
            expand->Release();
            sleep_short(220);
            debug_element(L"expanded", element);
            return SUCCEEDED(result);
        }
        expand->Release();
        return true;
    }
    if (select_item(element)) {
        return true;
    }
    return safe_click_element(element);
}

void send_ctrl_a() {
    debug_log(L"skip synthetic Ctrl+A input");
}

void send_key(WORD key) {
    (void)key;
    debug_log(L"skip synthetic keyboard input");
}

void send_enter() {
    send_key(VK_RETURN);
}

void send_text(const std::wstring& text) {
    (void)text;
    debug_log(L"skip synthetic text input");
}

std::vector<HWND> top_windows() {
    std::vector<HWND> windows;
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            if (IsWindowVisible(hwnd)) {
                auto* output = reinterpret_cast<std::vector<HWND>*>(param);
                output->push_back(hwnd);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&windows));
    HWND foreground = GetForegroundWindow();
    if (foreground) {
        HWND root = GetAncestor(foreground, GA_ROOT);
        if (root && std::find(windows.begin(), windows.end(), root) == windows.end()) {
            windows.insert(windows.begin(), root);
        }
    }
    return windows;
}

std::wstring window_title(HWND hwnd) {
    wchar_t title[512] = {};
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    return title;
}

std::wstring process_image_path(HWND hwnd) {
    DWORD process_id = 0;
    GetWindowThreadProcessId(hwnd, &process_id);
    if (!process_id) {
        return L"";
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process) {
        return L"";
    }

    wchar_t path[4096] = {};
    DWORD size = static_cast<DWORD>(std::size(path));
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        result.assign(path, size);
    }
    CloseHandle(process);
    return result;
}

bool is_zw3d_window(HWND hwnd) {
    std::wstring path = process_image_path(hwnd);
    std::transform(path.begin(), path.end(), path.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return path.find(L"\\zw3d.exe") != std::wstring::npos || path.find(L"/zw3d.exe") != std::wstring::npos;
}

bool point_on_zw3d_window(int x, int y) {
    POINT point = {x, y};
    HWND hwnd = WindowFromPoint(point);
    if (!hwnd) {
        return false;
    }
    HWND root = GetAncestor(hwnd, GA_ROOT);
    return root && is_zw3d_window(root);
}

bool child_inside_parent(IUIAutomationElement* child, IUIAutomationElement* parent) {
    const RECT child_rect = element_rect(child);
    const RECT parent_rect = element_rect(parent);
    return visible_screen_rect(child_rect) && usable_rect(parent_rect) &&
           child_rect.left >= parent_rect.left && child_rect.top >= parent_rect.top &&
           child_rect.right <= parent_rect.right && child_rect.bottom <= parent_rect.bottom;
}

int rect_width(const RECT& rect) {
    return rect.right - rect.left;
}

int rect_height(const RECT& rect) {
    return rect.bottom - rect.top;
}

bool rect_center_inside(const RECT& rect, const RECT& container) {
    if (!visible_screen_rect(rect) || !usable_rect(container)) {
        return false;
    }
    const int x = (rect.left + rect.right) / 2;
    const int y = (rect.top + rect.bottom) / 2;
    return x >= container.left && x <= container.right && y >= container.top && y <= container.bottom;
}

bool mostly_in_right_side(const RECT& rect, const RECT& root_rect) {
    if (!visible_screen_rect(rect) || !usable_rect(root_rect)) {
        return false;
    }
    const int root_width = rect_width(root_rect);
    if (root_width <= 0) {
        return false;
    }
    const int center_x = (rect.left + rect.right) / 2;
    return center_x >= root_rect.left + (root_width * 3) / 5;
}

bool point_inside_rect(int x, int y, const RECT& rect) {
    return usable_rect(rect) && x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

bool point_inside_rects(int x, int y, const std::vector<RECT>& rects) {
    return std::any_of(rects.begin(), rects.end(), [&](const RECT& rect) {
        return point_inside_rect(x, y, rect);
    });
}

bool rects_intersect(const RECT& lhs, const RECT& rhs) {
    return usable_rect(lhs) && usable_rect(rhs) &&
           lhs.left < rhs.right && lhs.right > rhs.left &&
           lhs.top < rhs.bottom && lhs.bottom > rhs.top;
}

bool element_overlaps_parent(IUIAutomationElement* child, IUIAutomationElement* parent) {
    const RECT child_rect = element_rect(child);
    const RECT parent_rect = element_rect(parent);
    return visible_screen_rect(child_rect) && usable_rect(parent_rect) &&
           rects_intersect(child_rect, parent_rect);
}

bool likely_cidu_element_name(const std::wstring& name) {
    return contains_key(name, L"cidu-agent") || contains_key(name, L"输入选型需求") ||
           contains_key(name, L"请选择模型") || contains_key(name, L"已确认模型") ||
           contains_key(name, L"匹配度") || contains_key(name, L"完整节点线路") ||
           contains_key(name, L"选择附件") || contains_key(name, L"移除附件") ||
           contains_key(name, L"发送") || contains_key(name, L"选中模型") ||
           contains_key(name, L"应用参数") || contains_key(name, L"已更新参数") ||
           contains_key(name, L"确认无误后可点");
}

bool looks_like_cidu_chat_block(const std::wstring& name) {
    const std::vector<std::wstring> lines = split_lines(name);
    if (lines.size() < 2) {
        return false;
    }
    for (const auto& line : lines) {
        if (line.find(L'=') != std::wstring::npos || contains_key(line, L"匹配度") ||
            contains_key(line, L"完整节点线路") || contains_key(line, L"候选模型") ||
            contains_key(line, L"识别到的检索线索") || contains_key(line, L"模板名称")) {
            return true;
        }
    }
    return false;
}

bool looks_like_parameter_label_text(const std::wstring& name) {
    const std::wstring text = trim(name);
    return !text.empty() &&
           text.find(L'\n') == std::wstring::npos &&
           text.find(L'\r') == std::wstring::npos &&
           text.find(L'=') == std::wstring::npos &&
           !likely_cidu_element_name(text) &&
           !looks_like_cidu_chat_block(text);
}

bool looks_like_parameter_value_row(const std::wstring& name) {
    return name.find(L'\n') != std::wstring::npos ||
           name.find(L'\r') != std::wstring::npos ||
           name.find(L'=') != std::wstring::npos ||
           (name.find(L'[') != std::wstring::npos && name.find(L']') != std::wstring::npos);
}

bool model_candidate_matches(const std::wstring& name, const std::vector<std::wstring>& search_names) {
    if (looks_like_parameter_value_row(name)) {
        return false;
    }
    if (looks_like_cidu_chat_block(name) || likely_cidu_element_name(name) ||
        contains_key(name, L"关键词") || contains_key(name, L"参数说明") ||
        contains_key(name, L"参数模板") || contains_key(name, L"完整节点线路")) {
        return false;
    }
    const std::wstring normalized_name = normalize_key(name);
    for (const auto& search_name : search_names) {
        const std::wstring normalized_search = normalize_key(search_name);
        if (normalized_search.empty()) {
            continue;
        }
        if (normalized_name == normalized_search) {
            return true;
        }
        if ((name.find(L".jyasm") != std::wstring::npos || name.find(L".jypar") != std::wstring::npos) &&
            normalized_name.find(normalized_search) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

bool element_supports_tree_action(IUIAutomationElement* element) {
    if (!element) {
        return false;
    }
    IUIAutomationExpandCollapsePattern* expand = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expand))) && expand) {
        expand->Release();
        return true;
    }
    IUIAutomationSelectionItemPattern* selection = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selection))) && selection) {
        selection->Release();
        return true;
    }
    IUIAutomationInvokePattern* invoke = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke))) && invoke) {
        invoke->Release();
        return true;
    }
    return false;
}

bool inside_excluded_rects(IUIAutomationElement* element, const std::vector<RECT>& excluded_rects) {
    const RECT rect = element_rect(element);
    return std::any_of(excluded_rects.begin(), excluded_rects.end(), [&](const RECT& excluded) {
        return rect_center_inside(rect, excluded);
    });
}

std::vector<RECT> collect_cidu_rects(UiAutomation& automation, IUIAutomationElement* root) {
    std::vector<RECT> rects;
    if (!automation || !root) {
        return rects;
    }
    IUIAutomationCondition* condition = nullptr;
    IUIAutomationElementArray* elements = nullptr;
    if (FAILED(automation.value->CreateTrueCondition(&condition)) || !condition) {
        return rects;
    }
    const HRESULT found = root->FindAll(TreeScope_Subtree, condition, &elements);
    condition->Release();
    if (FAILED(found) || !elements) {
        return rects;
    }

    int length = 0;
    elements->get_Length(&length);
    length = (length < 2500) ? length : 2500;
    for (int index = 0; index < length; ++index) {
        IUIAutomationElement* item = nullptr;
        if (FAILED(elements->GetElement(index, &item)) || !item) {
            continue;
        }
        const int type = element_control_type(item);
        if ((type == UIA_WindowControlTypeId || type == UIA_PaneControlTypeId || type == UIA_CustomControlTypeId) &&
            likely_cidu_element_name(element_name(item))) {
            const RECT rect = element_rect(item);
            if (visible_screen_rect(rect)) {
                rects.push_back(rect);
            }
        }
        item->Release();
    }
    elements->Release();
    if (!rects.empty()) {
        debug_log(L"exclude cidu rects=" + std::to_wstring(rects.size()));
    }
    return rects;
}

UiElement element_from_hwnd(UiAutomation& automation, HWND hwnd) {
    IUIAutomationElement* element = nullptr;
    if (automation && hwnd && SUCCEEDED(automation.value->ElementFromHandle(hwnd, &element))) {
        return UiElement(element);
    }
    return {};
}

bool window_looks_like_cidu(HWND hwnd, IUIAutomationElement* root) {
    const std::wstring title = window_title(hwnd);
    if (contains_key(title, L"Cidu") || contains_key(title, L"CiduZW3DAgentForm") ||
        contains_key(element_name(root), L"Cidu") || contains_key(element_name(root), L"CiduZW3DAgentForm")) {
        return true;
    }
    return false;
}

UiElement find_descendant_contains(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const std::vector<std::wstring>& names,
    int max_items = 2500,
    const std::vector<int>& allowed_control_types = {}) {
    if (!automation || !root) {
        return {};
    }
    IUIAutomationCondition* condition = nullptr;
    IUIAutomationElementArray* elements = nullptr;
    if (FAILED(automation.value->CreateTrueCondition(&condition)) || !condition) {
        return {};
    }
    const HRESULT found = root->FindAll(TreeScope_Subtree, condition, &elements);
    condition->Release();
    if (FAILED(found) || !elements) {
        return {};
    }

    int length = 0;
    elements->get_Length(&length);
    length = (length < max_items) ? length : max_items;
    for (int index = 0; index < length; ++index) {
        IUIAutomationElement* item = nullptr;
        if (FAILED(elements->GetElement(index, &item)) || !item) {
            continue;
        }
        const std::wstring name = element_name(item);
        const bool matched = std::any_of(names.begin(), names.end(), [&](const std::wstring& candidate) {
            return contains_key(name, candidate);
        });
        const int control_type = element_control_type(item);
        const bool allowed = allowed_control_types.empty() ||
                             std::find(allowed_control_types.begin(), allowed_control_types.end(), control_type) !=
                                 allowed_control_types.end();
        if (matched && allowed && visible_screen_rect(element_rect(item))) {
            debug_element(L"matched element", item);
            elements->Release();
            return UiElement(item);
        }
        item->Release();
    }
    elements->Release();
    return {};
}

UiElement find_first_descendant(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const std::function<bool(IUIAutomationElement*)>& predicate,
    int max_items = 2500) {
    if (!automation || !root) {
        return {};
    }
    IUIAutomationCondition* condition = nullptr;
    IUIAutomationElementArray* elements = nullptr;
    if (FAILED(automation.value->CreateTrueCondition(&condition)) || !condition) {
        return {};
    }
    const HRESULT found = root->FindAll(TreeScope_Subtree, condition, &elements);
    condition->Release();
    if (FAILED(found) || !elements) {
        return {};
    }

    int length = 0;
    elements->get_Length(&length);
    length = (length < max_items) ? length : max_items;
    for (int index = 0; index < length; ++index) {
        IUIAutomationElement* item = nullptr;
        if (FAILED(elements->GetElement(index, &item)) || !item) {
            continue;
        }
        if (predicate(item) && visible_screen_rect(element_rect(item))) {
            debug_element(L"matched by predicate", item);
            elements->Release();
            return UiElement(item);
        }
        item->Release();
    }
    elements->Release();
    return {};
}

UiElement find_descendant_contains_excluding(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const std::vector<std::wstring>& names,
    const std::vector<RECT>& excluded_rects,
    int max_items = 2500,
    const std::vector<int>& allowed_control_types = {}) {
    return find_first_descendant(
        automation,
        root,
        [&](IUIAutomationElement* item) {
            if (inside_excluded_rects(item, excluded_rects)) {
                return false;
            }
            const std::wstring name = element_name(item);
            const bool matched = std::any_of(names.begin(), names.end(), [&](const std::wstring& candidate) {
                return contains_key(name, candidate);
            });
            const int control_type = element_control_type(item);
            const bool allowed = allowed_control_types.empty() ||
                                 std::find(allowed_control_types.begin(), allowed_control_types.end(), control_type) !=
                                     allowed_control_types.end();
            return matched && allowed;
        },
        max_items);
}

UiElement find_child_pane_containing(UiAutomation& automation, IUIAutomationElement* root, const std::vector<std::wstring>& names) {
    IUIAutomationCondition* condition = nullptr;
    IUIAutomationElementArray* children = nullptr;
    if (!automation || !root || FAILED(automation.value->CreateTrueCondition(&condition)) || !condition) {
        return {};
    }
    const HRESULT found = root->FindAll(TreeScope_Children, condition, &children);
    condition->Release();
    if (FAILED(found) || !children) {
        return {};
    }
    int length = 0;
    children->get_Length(&length);
    for (int index = 0; index < length; ++index) {
        IUIAutomationElement* child = nullptr;
        if (FAILED(children->GetElement(index, &child)) || !child) {
            continue;
        }
        const int type = element_control_type(child);
        if ((type == UIA_PaneControlTypeId || type == UIA_WindowControlTypeId || type == UIA_CustomControlTypeId) &&
            find_descendant_contains(automation, child, names, 1200)) {
            debug_element(L"use child pane", child);
            children->Release();
            return UiElement(child);
        }
        child->Release();
    }
    children->Release();
    return {};
}

UiElement find_jiavu_panel_in_root(UiAutomation& automation, IUIAutomationElement* root) {
    if (!automation || !root) {
        return {};
    }
    const std::vector<RECT> excluded_rects = collect_cidu_rects(automation, root);
    const RECT root_rect = element_rect(root);
    struct MarkerSpec {
        const wchar_t* name;
        std::vector<int> types;
        int weight;
    };
    const std::vector<MarkerSpec> tab_markers = {
        {L"第三方库", {UIA_TabItemControlTypeId, UIA_ButtonControlTypeId, UIA_TextControlTypeId}, 3},
        {L"自定义库", {UIA_TabItemControlTypeId, UIA_ButtonControlTypeId, UIA_TextControlTypeId}, 3},
        {L"选型参数", {UIA_TabItemControlTypeId, UIA_ButtonControlTypeId, UIA_TextControlTypeId}, 3},
        {L"搜索结果", {UIA_TabItemControlTypeId, UIA_ButtonControlTypeId, UIA_TextControlTypeId}, 2},
        {L"常用工具", {UIA_TabItemControlTypeId, UIA_ButtonControlTypeId, UIA_TextControlTypeId}, 2},
    };
    const std::vector<MarkerSpec> grid_markers = {
        {L"参数名称", {UIA_TextControlTypeId, UIA_CustomControlTypeId, UIA_DataItemControlTypeId}, 2},
        {L"参数值", {UIA_TextControlTypeId, UIA_CustomControlTypeId, UIA_DataItemControlTypeId}, 2},
        {L"备注", {UIA_TextControlTypeId, UIA_CustomControlTypeId, UIA_DataItemControlTypeId}, 1},
        {L"产品名称", {UIA_TextControlTypeId, UIA_EditControlTypeId, UIA_CustomControlTypeId}, 1},
        {L"规格型号", {UIA_TextControlTypeId, UIA_CustomControlTypeId, UIA_DataItemControlTypeId}, 1},
        {L"物料编码", {UIA_TextControlTypeId, UIA_CustomControlTypeId, UIA_DataItemControlTypeId}, 1},
        {L"库存", {UIA_TextControlTypeId, UIA_CustomControlTypeId, UIA_DataItemControlTypeId}, 1},
        {L"获取", {UIA_ButtonControlTypeId, UIA_TextControlTypeId}, 1},
    };
    const std::vector<MarkerSpec> action_markers = {
        {L"插入", {UIA_ButtonControlTypeId}, 2},
        {L"打开", {UIA_ButtonControlTypeId}, 2},
    };
    const std::vector<std::wstring> cidu_markers = {
        L"cidu-agent", L"输入选型需求", L"选择附件", L"移除附件", L"应用参数",
        L"发送", L"选中模型", L"请选择模型"
    };
    IUIAutomationCondition* condition = nullptr;
    IUIAutomationElementArray* elements = nullptr;
    if (FAILED(automation.value->CreateTrueCondition(&condition)) || !condition) {
        return {};
    }
    const HRESULT found = root->FindAll(TreeScope_Subtree, condition, &elements);
    condition->Release();
    if (FAILED(found) || !elements) {
        return {};
    }

    IUIAutomationElement* best = nullptr;
    int best_score = 0;
    long long best_area = (std::numeric_limits<long long>::max)();

    int length = 0;
    elements->get_Length(&length);
    length = (length < 3600) ? length : 3600;
    for (int index = 0; index < length; ++index) {
        IUIAutomationElement* item = nullptr;
        if (FAILED(elements->GetElement(index, &item)) || !item) {
            continue;
        }
            const int type = element_control_type(item);
            if (type != UIA_PaneControlTypeId && type != UIA_WindowControlTypeId && type != UIA_CustomControlTypeId) {
            item->Release();
            continue;
            }
            const RECT rect = element_rect(item);
            if (!visible_screen_rect(rect)) {
            item->Release();
            continue;
            }
            if (inside_excluded_rects(item, excluded_rects)) {
            item->Release();
            continue;
            }
            const int width = rect.right - rect.left;
            const int height = rect.bottom - rect.top;
            if (width < 260 || height < 300) {
            item->Release();
            continue;
            }
            const int root_width = rect_width(root_rect);
            const int center_x = (rect.left + rect.right) / 2;
            const bool right_half = root_width > 0 && center_x >= root_rect.left + (root_width * 3) / 5;
            const bool near_right_edge = std::abs(rect.right - root_rect.right) <= 160;
            if (!right_half && !near_right_edge) {
                item->Release();
                continue;
            }
            if (find_descendant_contains_excluding(
                    automation,
                    item,
                    cidu_markers,
                    {},
                    1200,
                    {UIA_TextControlTypeId, UIA_ButtonControlTypeId, UIA_EditControlTypeId, UIA_CustomControlTypeId})) {
                item->Release();
                continue;
            }
            if (find_first_descendant(
                    automation,
                    item,
                    [&](IUIAutomationElement* child) {
                        const std::wstring name = element_name(child);
                        return likely_cidu_element_name(name) || looks_like_cidu_chat_block(name);
                    },
                    1200)) {
                item->Release();
                continue;
            }
            int tab_score = 0;
            int grid_score = 0;
            int action_score = 0;
            auto accumulate_score = [&](const std::vector<MarkerSpec>& markers, int& bucket) {
                for (const auto& marker : markers) {
                    UiElement matched = find_descendant_contains_excluding(
                        automation,
                        item,
                        {marker.name},
                        excluded_rects,
                        1200,
                        marker.types);
                    if (matched && child_inside_parent(matched.value, item)) {
                        bucket += marker.weight;
                    }
                }
            };
            accumulate_score(tab_markers, tab_score);
            accumulate_score(grid_markers, grid_score);
            accumulate_score(action_markers, action_score);
            int score = tab_score + grid_score + action_score;
            if (right_half) {
                score += 4;
            }
            if (near_right_edge) {
                score += 3;
            }
            const bool plausible_panel =
                tab_score >= 3 ||
                (tab_score >= 5 && (grid_score >= 2 || action_score >= 2)) ||
                (grid_score >= 4 && action_score >= 2);
            if (!plausible_panel) {
                item->Release();
                continue;
            }
        const long long area = static_cast<long long>(width) * static_cast<long long>(height);
        if (score >= 2 && (score > best_score || (score == best_score && area < best_area))) {
            release_ptr(best);
            best = item;
            best->AddRef();
            best_score = score;
            best_area = area;
        }
        item->Release();
    }
    elements->Release();
    if (best) {
        debug_log(L"use jiavu panel score=" + std::to_wstring(best_score) + L" area=" + std::to_wstring(best_area));
        debug_element(L"use jiavu panel element", best);
        return UiElement(best);
    }
    return {};
}

UiElement jiavu_panel_or_root(UiAutomation& automation, IUIAutomationElement* root) {
    UiElement panel = find_jiavu_panel_in_root(automation, root);
    if (panel) {
        return panel;
    }
    if (root) {
        root->AddRef();
    }
    return UiElement(root);
}

UiElement find_jiavu_child_window(UiAutomation& automation, IUIAutomationElement* root) {
    if (!automation || !root) {
        return {};
    }
    return find_first_descendant(
        automation,
        root,
        [](IUIAutomationElement* item) {
            const int type = element_control_type(item);
            if (type != UIA_WindowControlTypeId && type != UIA_PaneControlTypeId) {
                return false;
            }
            return contains_key(element_name(item), L"佳鱼参数化设计选型");
        },
        1800);
}

struct JiavuMarkers {
    bool custom_library = false;
    bool parameter_tab = false;
    bool parameter_grid = false;
    bool search_result = false;
    bool insert_button = false;
    bool open_button = false;

    bool looks_open() const {
        return custom_library || search_result || parameter_tab ||
               (parameter_grid && (insert_button || open_button)) ||
               (insert_button && open_button);
    }
};

JiavuMarkers detect_jiavu_markers(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const std::vector<RECT>& excluded_rects = {}) {
    JiavuMarkers markers;
    auto has_visible_marker = [&](const std::vector<std::wstring>& names, int max_items, const std::vector<int>& types) {
        return static_cast<bool>(find_first_descendant(
            automation,
            root,
            [&](IUIAutomationElement* item) {
                if (inside_excluded_rects(item, excluded_rects)) {
                    return false;
                }
                const int type = element_control_type(item);
                if (!types.empty() && std::find(types.begin(), types.end(), type) == types.end()) {
                    return false;
                }
                const std::wstring name = element_name(item);
                return std::any_of(names.begin(), names.end(), [&](const std::wstring& candidate) {
                    return contains_key(name, candidate);
                });
            },
            max_items));
    };
    markers.custom_library = has_visible_marker(
        {L"自定义库"},
        1400,
        {UIA_TabItemControlTypeId, UIA_ButtonControlTypeId, UIA_TextControlTypeId});
    markers.parameter_tab = has_visible_marker(
        {L"选型参数"},
        1400,
        {UIA_TabItemControlTypeId, UIA_ButtonControlTypeId, UIA_TextControlTypeId});
    markers.parameter_grid = has_visible_marker(
        {L"参数名称", L"参数值", L"规格型号", L"物料编码", L"库存"},
        2200,
        {UIA_TextControlTypeId, UIA_EditControlTypeId, UIA_DataItemControlTypeId, UIA_CustomControlTypeId});
    markers.search_result = has_visible_marker(
        {L"搜索结果"},
        1400,
        {UIA_TabItemControlTypeId, UIA_ButtonControlTypeId, UIA_TextControlTypeId});
    markers.insert_button = has_visible_marker(
        {L"插入"},
        1800,
        {UIA_ButtonControlTypeId});
    markers.open_button = has_visible_marker(
        {L"打开"},
        1800,
        {UIA_ButtonControlTypeId});
    return markers;
}

JiavuMarkers detect_jiavu_markers_old(UiAutomation& automation, IUIAutomationElement* root) {
    JiavuMarkers markers;
    markers.custom_library = static_cast<bool>(find_descendant_contains(
        automation,
        root,
        {L"自定义库"},
        1400,
        {UIA_TabItemControlTypeId, UIA_ButtonControlTypeId, UIA_TextControlTypeId}));
    markers.parameter_tab = static_cast<bool>(find_descendant_contains(
        automation,
        root,
        {L"选型参数"},
        1400,
        {UIA_TabItemControlTypeId, UIA_ButtonControlTypeId, UIA_TextControlTypeId}));
    markers.parameter_grid = static_cast<bool>(find_descendant_contains(
        automation,
        root,
        {L"参数名称", L"参数值", L"规格型号", L"物料编码", L"库存"},
        2200,
        {UIA_TextControlTypeId, UIA_EditControlTypeId, UIA_DataItemControlTypeId, UIA_CustomControlTypeId}));
    markers.search_result = static_cast<bool>(find_descendant_contains(
        automation,
        root,
        {L"搜索结果"},
        1400,
        {UIA_TabItemControlTypeId, UIA_ButtonControlTypeId, UIA_TextControlTypeId}));
    markers.insert_button = static_cast<bool>(find_descendant_contains(
        automation,
        root,
        {L"插入"},
        1800,
        {UIA_ButtonControlTypeId}));
    markers.open_button = static_cast<bool>(find_descendant_contains(
        automation,
        root,
        {L"打开"},
        1800,
        {UIA_ButtonControlTypeId}));
    return markers;
}

std::wstring marker_text(const JiavuMarkers& markers) {
    std::wostringstream out;
    out << L"custom=" << markers.custom_library
        << L" params=" << markers.parameter_tab
        << L" grid=" << markers.parameter_grid
        << L" search=" << markers.search_result
        << L" insert=" << markers.insert_button
        << L" open=" << markers.open_button;
    return out.str();
}

UiElement find_jiavu_root(UiAutomation& automation) {
    for (HWND hwnd : top_windows()) {
        if (!is_zw3d_window(hwnd)) {
            continue;
        }
        UiElement root = element_from_hwnd(automation, hwnd);
        if (!root) {
            continue;
        }
        if (window_looks_like_cidu(hwnd, root.value)) {
            debug_log(L"skip Cidu window title=" + window_title(hwnd));
            continue;
        }
        const std::wstring title = window_title(hwnd);
        const std::vector<RECT> excluded_rects = collect_cidu_rects(automation, root.value);
        UiElement jiavu_child = find_jiavu_child_window(automation, root.value);
        if (jiavu_child) {
            UiElement child_panel = find_jiavu_panel_in_root(automation, jiavu_child.value);
            JiavuMarkers child_markers = detect_jiavu_markers(automation, jiavu_child.value, excluded_rects);
            debug_log(L"jiavu child markers: " + marker_text(child_markers));
            if (child_panel) {
                SetForegroundWindow(hwnd);
                debug_element(L"use jiavu child panel", child_panel.value);
                return child_panel;
            }
            if (child_markers.looks_open()) {
                SetForegroundWindow(hwnd);
                debug_element(L"use jiavu child window", jiavu_child.value);
                return jiavu_child;
            }
            debug_element(L"skip jiavu child without visible markers", jiavu_child.value);
        }
        if (contains_key(title, L"佳鱼参数化设计选型") || contains_key(element_name(root.value), L"佳鱼参数化设计选型")) {
            UiElement panel = find_jiavu_panel_in_root(automation, root.value);
            JiavuMarkers title_markers = detect_jiavu_markers(automation, root.value, excluded_rects);
            debug_log(L"jiavu title window markers: " + marker_text(title_markers));
            if (panel) {
                SetForegroundWindow(hwnd);
                debug_log(L"use jiavu window panel by title=" + title);
                return panel;
            }
            if (title_markers.looks_open()) {
                SetForegroundWindow(hwnd);
                debug_log(L"use jiavu window by title=" + title);
                return root;
            }
            debug_log(L"skip jiavu title window without visible markers title=" + title);
            continue;
        }
        debug_log(L"scan window title=" + title + L" root=" + element_name(root.value));
        JiavuMarkers markers = detect_jiavu_markers(automation, root.value, excluded_rects);
        debug_log(L"jiavu markers: " + marker_text(markers));
        if (markers.looks_open()) {
            SetForegroundWindow(hwnd);
            debug_log(L"use jiavu window by controls title=" + title);
            return jiavu_panel_or_root(automation, root.value);
        }

        UiElement model_input = find_descendant_contains(
            automation,
            root.value,
            {L"模型输入"},
            2200,
            {UIA_ButtonControlTypeId, UIA_TextControlTypeId});
        if (model_input) {
            SetForegroundWindow(hwnd);
            debug_element(L"open jiavu by model input", model_input.value);
            invoke_or_click(model_input.value);
            sleep_short(900);
            UiElement refreshed = element_from_hwnd(automation, hwnd);
            if (refreshed) {
                markers = detect_jiavu_markers(automation, refreshed.value, collect_cidu_rects(automation, refreshed.value));
                debug_log(L"jiavu markers after model input: " + marker_text(markers));
                if (markers.looks_open()) {
                    return jiavu_panel_or_root(automation, refreshed.value);
                }
            }
        }
    }
    return {};
}

std::vector<std::wstring> model_search_names(const JiavuAutomationRequest& request) {
    std::vector<std::wstring> names;
    auto add = [&](const std::wstring& value) {
        const std::wstring item = trim(value);
        if (!item.empty() && std::none_of(names.begin(), names.end(), [&](const std::wstring& seen) {
                return normalize_key(seen) == normalize_key(item);
            })) {
            names.push_back(item);
        }
    };
    add(request.selected_model);
    add(request.template_name);
    add(base_template_name(request.template_name));
    const std::vector<std::wstring> parts = split_path(request.full_node_path);
    if (!parts.empty()) {
        add(parts.back());
        add(base_template_name(parts.back()));
    }
    return names;
}

std::wstring model_leaf_search_query(const JiavuAutomationRequest& request) {
    const std::vector<std::wstring> parts = split_path(request.full_node_path);
    if (!parts.empty()) {
        const std::wstring leaf = base_template_name(parts.back());
        if (!trim(leaf).empty()) {
            return leaf;
        }
    }
    const std::wstring template_leaf = base_template_name(request.template_name);
    if (!trim(template_leaf).empty()) {
        return template_leaf;
    }
    return request.selected_model;
}

std::vector<std::wstring> effective_model_path_parts(const JiavuAutomationRequest& request) {
    std::vector<std::wstring> parts = split_path(request.full_node_path);
    const std::wstring leaf =
        !parts.empty() ? base_template_name(parts.back()) : base_template_name(request.template_name);
    if (parts.size() <= 2 && contains_key(leaf, L"斜梯")) {
        return {L"4.通用设计", L"平台斜梯围栏", L"斜梯", L"斜梯.jyasm"};
    }
    return parts;
}

bool select_custom_library(UiAutomation& automation, IUIAutomationElement* root) {
    UiElement tab = find_descendant_contains(
        automation,
        root,
        {L"自定义库"},
        1800,
        {UIA_TabItemControlTypeId, UIA_ButtonControlTypeId, UIA_TextControlTypeId});
    if (!tab) {
        return false;
    }
    return invoke_or_click(tab.value);
}

bool set_edit_value(IUIAutomationElement* edit, const std::wstring& value) {
    if (!edit) {
        return false;
    }
    if (element_control_type(edit) == UIA_ComboBoxControlTypeId) {
        return false;
    }
    IUIAutomationValuePattern* value_pattern = nullptr;
    if (SUCCEEDED(edit->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value_pattern))) && value_pattern) {
        BOOL is_read_only = FALSE;
        if (SUCCEEDED(value_pattern->get_CurrentIsReadOnly(&is_read_only)) && is_read_only) {
            value_pattern->Release();
            debug_element(L"skip readonly value target", edit);
            return false;
        }

        BSTR raw_value = SysAllocString(value.c_str());
        const HRESULT result = value_pattern->SetValue(raw_value);
        SysFreeString(raw_value);
        value_pattern->Release();
        sleep_short(120);
        if (SUCCEEDED(result)) {
            debug_element(L"set edit value", edit);
            return true;
        }
        debug_element(L"failed ValuePattern set", edit);
    }

    IUIAutomationLegacyIAccessiblePattern* legacy = nullptr;
    if (SUCCEEDED(edit->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId, IID_PPV_ARGS(&legacy))) && legacy) {
        const HRESULT result = legacy->SetValue(value.c_str());
        legacy->Release();
        sleep_short(120);
        if (SUCCEEDED(result)) {
            debug_element(L"set legacy value", edit);
            return true;
        }
        debug_element(L"failed LegacyIAccessible set", edit);
    }
    HWND hwnd = element_native_window(edit);
    if (hwnd && IsWindow(hwnd) && point_on_zw3d_window((element_rect(edit).left + element_rect(edit).right) / 2,
                                                       (element_rect(edit).top + element_rect(edit).bottom) / 2)) {
        SendMessageW(hwnd, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(value.c_str()));
        HWND parent = GetParent(hwnd);
        if (parent) {
            SendMessageW(
                parent,
                WM_COMMAND,
                MAKEWPARAM(GetDlgCtrlID(hwnd), EN_CHANGE),
                reinterpret_cast<LPARAM>(hwnd));
        }
        sleep_short(120);
        debug_element(L"set native window text", edit);
        return true;
    }
    debug_element(L"skip target without writable value pattern", edit);
    return false;
}

BOOL CALLBACK redraw_child_window(HWND hwnd, LPARAM) {
    if (!IsWindow(hwnd)) {
        return TRUE;
    }
    RedrawWindow(
        hwnd,
        nullptr,
        nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    return TRUE;
}

void refresh_window_tree(HWND hwnd) {
    (void)hwnd;
    debug_log(
        L"refresh window tree skipped: disabled to avoid ZW3D crash after DataProp write");
}

std::wstring displayed_parameter_value_from_text(
    const std::wstring& text,
    const std::wstring& parameter_name) {
    const std::wstring requested = normalize_key(parameter_name);
    if (requested.empty()) {
        return L"";
    }
    const std::vector<std::wstring> lines = split_lines(text);
    for (const auto& line : lines) {
        std::size_t split_pos = std::wstring::npos;
        for (wchar_t delimiter : {L'=', L'：', L':'}) {
            const auto delimiter_pos = line.find(delimiter);
            if (delimiter_pos != std::wstring::npos) {
                split_pos = delimiter_pos;
                break;
            }
        }
        if (split_pos == std::wstring::npos) {
            continue;
        }
        const std::wstring left = line.substr(0, split_pos);
        if (normalize_key(left) == requested) {
            return trim(line.substr(split_pos + 1));
        }
    }
    return L"";
}

int confirm_displayed_parameters(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const std::vector<OrderedParameter>& parameters,
    const std::vector<RECT>& excluded_rects) {
    IUIAutomationCondition* condition = nullptr;
    IUIAutomationElementArray* elements = nullptr;
    if (!automation || !root || FAILED(automation.value->CreateTrueCondition(&condition)) || !condition) {
        return 0;
    }
    const HRESULT found = root->FindAll(TreeScope_Subtree, condition, &elements);
    condition->Release();
    if (FAILED(found) || !elements) {
        return 0;
    }

    int confirmed = 0;
    int length = 0;
    elements->get_Length(&length);
    length = (length < 3500) ? length : 3500;
    for (const auto& parameter : parameters) {
        if (trim(parameter.value).empty() || contains_key(parameter.name, L"关键词")) {
            continue;
        }
        bool matched = false;
        for (int index = 0; index < length && !matched; ++index) {
            IUIAutomationElement* item = nullptr;
            if (FAILED(elements->GetElement(index, &item)) || !item) {
                continue;
            }
            const int type = element_control_type(item);
            const bool usable_type =
                type == UIA_TextControlTypeId || type == UIA_EditControlTypeId ||
                type == UIA_ComboBoxControlTypeId || type == UIA_DataItemControlTypeId ||
                type == UIA_CustomControlTypeId;
            const std::wstring name = usable_type ? element_name(item) : L"";
            if (usable_type && !inside_excluded_rects(item, excluded_rects)) {
                const std::wstring displayed = displayed_parameter_value_from_text(name, parameter.name);
                if (!displayed.empty()) {
                    debug_log(
                        L"display check parameter=" + parameter.name +
                        L" expected=" + parameter.value +
                        L" displayed=" + displayed);
                    if (normalize_key(displayed) == normalize_key(parameter.value)) {
                        matched = true;
                        ++confirmed;
                    }
                }
            }
            item->Release();
        }
        if (!matched) {
            debug_log(
                L"display check not confirmed parameter=" + parameter.name +
                L" expected=" + parameter.value);
        }
    }
    elements->Release();
    return confirmed;
}

bool element_has_writable_value(IUIAutomationElement* element) {
    if (!element || element_control_type(element) == UIA_ComboBoxControlTypeId) {
        return false;
    }
    IUIAutomationValuePattern* value_pattern = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value_pattern))) && value_pattern) {
        BOOL is_read_only = FALSE;
        const bool writable = FAILED(value_pattern->get_CurrentIsReadOnly(&is_read_only)) || !is_read_only;
        value_pattern->Release();
        if (writable) {
            return true;
        }
    }
    IUIAutomationLegacyIAccessiblePattern* legacy = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId, IID_PPV_ARGS(&legacy))) && legacy) {
        legacy->Release();
        return true;
    }
    return false;
}

bool set_accessible_element_value(
    IUIAutomationElement* element,
    const std::wstring& value,
    const std::wstring& purpose) {
    if (!element || trim(value).empty()) {
        return false;
    }

    IUIAutomationValuePattern* value_pattern = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value_pattern))) &&
        value_pattern) {
        BOOL is_read_only = FALSE;
        const bool read_only =
            SUCCEEDED(value_pattern->get_CurrentIsReadOnly(&is_read_only)) && is_read_only;
        if (!read_only) {
            BSTR raw_value = SysAllocString(value.c_str());
            const HRESULT result = value_pattern->SetValue(raw_value);
            SysFreeString(raw_value);
            value_pattern->Release();
            sleep_short(140);
            if (SUCCEEDED(result)) {
                debug_element(L"set accessible ValuePattern " + purpose, element);
                return true;
            }
            debug_element(L"failed accessible ValuePattern " + purpose, element);
        } else {
            value_pattern->Release();
            debug_element(L"skip readonly accessible ValuePattern " + purpose, element);
        }
    }

    IUIAutomationLegacyIAccessiblePattern* legacy = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId, IID_PPV_ARGS(&legacy))) &&
        legacy) {
        const HRESULT result = legacy->SetValue(value.c_str());
        legacy->Release();
        sleep_short(140);
        if (SUCCEEDED(result)) {
            debug_element(L"set accessible LegacyIAccessible " + purpose, element);
            return true;
        }
        debug_element(L"failed accessible LegacyIAccessible " + purpose, element);
    }

    return false;
}

bool invoke_accessible_action(IUIAutomationElement* element, const std::wstring& purpose) {
    if (!element) {
        return false;
    }
    IUIAutomationInvokePattern* invoke = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke))) && invoke) {
        const HRESULT result = invoke->Invoke();
        invoke->Release();
        sleep_short(160);
        if (SUCCEEDED(result)) {
            debug_element(L"UIA invoke " + purpose, element);
            return true;
        }
    }

    IUIAutomationLegacyIAccessiblePattern* legacy = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId, IID_PPV_ARGS(&legacy))) && legacy) {
        const HRESULT result = legacy->DoDefaultAction();
        legacy->Release();
        sleep_short(160);
        if (SUCCEEDED(result)) {
            debug_element(L"UIA legacy default action " + purpose, element);
            return true;
        }
    }

    IUIAutomationSelectionItemPattern* selection = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selection))) && selection) {
        const HRESULT result = selection->Select();
        selection->Release();
        sleep_short(120);
        if (SUCCEEDED(result)) {
            debug_element(L"UIA select " + purpose, element);
            return true;
        }
    }
    debug_element(L"UIA action unavailable " + purpose, element);
    return false;
}

std::vector<std::wstring> parameter_lookup_names(const std::wstring& name) {
    std::vector<std::wstring> names;
    auto add = [&](const std::wstring& value) {
        const std::wstring clean = trim(value);
        if (clean.empty()) {
            return;
        }
        const std::wstring key = normalize_key(clean);
        const bool exists = std::any_of(names.begin(), names.end(), [&](const std::wstring& item) {
            return normalize_key(item) == key;
        });
        if (!exists) {
            names.push_back(clean);
        }
    };
    add(name);
    const auto unit_pos = name.find(L'[');
    if (unit_pos != std::wstring::npos) {
        add(name.substr(0, unit_pos));
    }
    const auto cn_unit_pos = name.find(L'【');
    if (cn_unit_pos != std::wstring::npos) {
        add(name.substr(0, cn_unit_pos));
    }
    return names;
}

bool parameter_text_matches(const std::wstring& text, const std::vector<std::wstring>& names) {
    if (trim(text).empty() || likely_cidu_element_name(text) || looks_like_cidu_chat_block(text)) {
        return false;
    }
    return std::any_of(names.begin(), names.end(), [&](const std::wstring& name) {
        return contains_key(text, name);
    });
}

UiElement find_accessible_parameter_row(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const std::vector<std::wstring>& names,
    const std::vector<RECT>& excluded_rects) {
    return find_first_descendant(
        automation,
        root,
        [&](IUIAutomationElement* item) {
            if (inside_excluded_rects(item, excluded_rects) || !element_overlaps_parent(item, root)) {
                return false;
            }
            const int type = element_control_type(item);
            if (type != UIA_ListItemControlTypeId && type != UIA_DataItemControlTypeId &&
                type != UIA_CustomControlTypeId && type != UIA_TextControlTypeId) {
                return false;
            }
            return parameter_text_matches(element_name(item), names);
        },
        4500);
}

bool writable_value_target_for_row(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const RECT& row_rect,
    const std::vector<RECT>& excluded_rects,
    UiElement& output) {
    auto is_same_parameter_row_target = [&](IUIAutomationElement* item, const RECT& rect) {
        if (!visible_screen_rect(rect) || !element_overlaps_parent(item, root)) {
            return false;
        }
        const int row_mid = (row_rect.top + row_rect.bottom) / 2;
        const int mid = (rect.top + rect.bottom) / 2;
        const bool same_row = std::abs(mid - row_mid) <= 42;
        const bool horizontally_related =
            rect.left >= row_rect.left - 12 &&
            rect.right <= row_rect.right + 260 &&
            rect.right > row_rect.left + 80 &&
            rect.left < row_rect.right + 80;
        if (!same_row || !horizontally_related) {
            debug_element(L"reject writable target outside parameter row", item);
            return false;
        }
        return true;
    };

    IUIAutomationElement* focused = nullptr;
    if (automation && SUCCEEDED(automation.value->GetFocusedElement(&focused)) && focused) {
        const RECT focused_rect = element_rect(focused);
        const bool usable =
            !inside_excluded_rects(focused, excluded_rects) &&
            is_same_parameter_row_target(focused, focused_rect) &&
            (element_control_type(focused) == UIA_ComboBoxControlTypeId ||
             element_has_writable_value(focused));
        if (usable) {
            debug_element(L"UIA row focused writable target", focused);
            output = UiElement(focused);
            return true;
        }
        focused->Release();
    }

    output = find_first_descendant(
        automation,
        root,
        [&](IUIAutomationElement* item) {
            if (inside_excluded_rects(item, excluded_rects) || !element_overlaps_parent(item, root)) {
                return false;
            }
            const int type = element_control_type(item);
            if (type != UIA_EditControlTypeId && type != UIA_ComboBoxControlTypeId &&
                type != UIA_DataItemControlTypeId && type != UIA_CustomControlTypeId) {
                return false;
            }
            const RECT rect = element_rect(item);
            return is_same_parameter_row_target(item, rect) &&
                   (type == UIA_ComboBoxControlTypeId || element_has_writable_value(item));
        },
        4500);
    return static_cast<bool>(output);
}

ParameterApplyResult apply_parameters_by_accessible_rows(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const std::vector<OrderedParameter>& parameters,
    const std::vector<RECT>& excluded_rects) {
    ParameterApplyResult result;
    result.requested = writable_parameter_count(parameters);
    if (!automation || !root || parameters.empty()) {
        return result;
    }

    for (const auto& parameter : parameters) {
        if (trim(parameter.value).empty() || contains_key(parameter.name, L"关键词")) {
            continue;
        }
        const std::vector<std::wstring> names = parameter_lookup_names(parameter.name);
        UiElement row = find_accessible_parameter_row(automation, root, names, excluded_rects);
        if (!row) {
            debug_log(L"UIA row write: parameter row not found=" + parameter.name);
            continue;
        }
        debug_element(L"UIA row write parameter row", row.value);
        const RECT row_rect = element_rect(row.value);
        if (set_accessible_element_value(row.value, parameter.value, L"parameter row")) {
            ++result.applied;
            debug_log(
                L"UIA row direct value parameter=" + parameter.name +
                L" value=" + parameter.value);
            continue;
        }
        if (!invoke_accessible_action(row.value, L"parameter row")) {
            continue;
        }
        UiElement target;
        if (!writable_value_target_for_row(automation, root, row_rect, excluded_rects, target)) {
            debug_log(L"UIA row write: no writable target after action parameter=" + parameter.name);
            continue;
        }
        const bool written =
            element_control_type(target.value) == UIA_ComboBoxControlTypeId
                ? set_combo_value(automation, target.value, parameter.value)
                : set_edit_value(target.value, parameter.value);
        if (written) {
            ++result.applied;
            debug_log(
                L"UIA row write parameter=" + parameter.name +
                L" value=" + parameter.value);
        } else {
            debug_log(L"UIA row write failed parameter=" + parameter.name);
        }
    }
    if (result.applied > 0) {
        result.displayed = confirm_displayed_parameters(automation, root, parameters, excluded_rects);
        result.page_refresh_confirmed = result.displayed >= result.applied;
    }
    return result;
}

ParameterApplyResult apply_parameters_by_visible_controls(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const std::vector<OrderedParameter>& parameters,
    const std::vector<RECT>& excluded_rects) {
    ParameterApplyResult result;
    result.requested = writable_parameter_count(parameters);
    if (!automation || !root || parameters.empty()) {
        return result;
    }

    for (const auto& parameter : parameters) {
        const std::wstring& name = parameter.name;
        const std::wstring& value = parameter.value;
        if (trim(value).empty() || contains_key(name, L"关键词")) {
            continue;
        }

        std::vector<std::wstring> names = parameter_lookup_names(name);
        const auto unit_pos = name.find(L'[');
        if (unit_pos != std::wstring::npos) {
            names.push_back(name.substr(0, unit_pos));
        }

        UiElement target;
        UiElement label = find_parameter_label(automation, root, names, excluded_rects);
        if (label) {
            debug_element(L"visible control parameter label", label.value);
            target = find_value_control_from_multiline_table(automation, root, names, excluded_rects, false);
            if (!target) {
                target = find_edit_right_of_label(automation, root, label.value);
            }
        } else {
            target = find_value_control_from_multiline_table(automation, root, names, excluded_rects, false);
            debug_log(L"visible control label not found=" + name);
        }

        if (!target) {
            debug_log(L"visible control target not found=" + name);
            continue;
        }

        debug_element(L"visible control value target", target.value);
        const bool written =
            element_control_type(target.value) == UIA_ComboBoxControlTypeId
                ? set_combo_value(automation, target.value, value)
                : set_edit_value(target.value, value);
        if (written) {
            ++result.applied;
            debug_log(
                L"visible control write parameter=" + name +
                L" value=" + value);
        } else {
            debug_log(L"visible control write failed=" + name);
        }
    }

    if (result.applied > 0) {
        result.displayed = confirm_displayed_parameters(automation, root, parameters, excluded_rects);
        result.page_refresh_confirmed = result.displayed >= result.applied;
        debug_log(
            L"visible control display confirmed=" + std::to_wstring(result.displayed) +
            L" applied=" + std::to_wstring(result.applied));
    }
    return result;
}

bool refresh_parameter_tab_by_accessible_actions(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const std::vector<RECT>& excluded_rects) {
    if (!automation || !root) {
        return false;
    }
    bool switched_away = false;
    for (const std::wstring& name : {L"搜索结果", L"自定义库", L"第三方库"}) {
        UiElement tab = find_descendant_contains_excluding(
            automation,
            root,
            {name},
            excluded_rects,
            2500,
            {UIA_TabItemControlTypeId, UIA_ButtonControlTypeId, UIA_TextControlTypeId});
        if (tab && invoke_accessible_action(tab.value, L"switch away from parameter tab")) {
            switched_away = true;
            break;
        }
    }
    if (switched_away) {
        sleep_short(180);
    }
    UiElement parameter_tab = find_descendant_contains_excluding(
        automation,
        root,
        {L"选型参数"},
        excluded_rects,
        2500,
        {UIA_TabItemControlTypeId, UIA_ButtonControlTypeId, UIA_TextControlTypeId});
    if (!parameter_tab) {
        debug_log(L"parameter tab refresh: tab not found");
        return false;
    }
    const bool invoked = invoke_accessible_action(parameter_tab.value, L"return to parameter tab");
    if (invoked) {
        sleep_short(350);
    }
    debug_log(
        L"parameter tab refresh: switched_away=" + std::to_wstring(switched_away ? 1 : 0) +
        L" returned=" + std::to_wstring(invoked ? 1 : 0));
    return invoked;
}

bool search_model(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const std::wstring& query,
    const std::vector<RECT>& excluded_rects = {}) {
    const std::wstring clean_query = clean_model_text(base_template_name(query));
    if (clean_query.empty()) {
        return false;
    }
    UiElement edit = find_first_descendant(
        automation,
        root,
        [&](IUIAutomationElement* item) {
            if (inside_excluded_rects(item, excluded_rects) || !child_inside_parent(item, root)) {
                return false;
            }
            return element_control_type(item) == UIA_EditControlTypeId &&
                   !likely_cidu_element_name(element_name(item)) &&
                   !looks_like_cidu_chat_block(element_name(item));
        },
        1800);
    if (!edit || !set_edit_value(edit.value, clean_query)) {
        return false;
    }
    UiElement search_button = find_descendant_contains_excluding(
        automation,
        root,
        {L"搜索", L"查找"},
        excluded_rects,
        2200,
        {UIA_ButtonControlTypeId});
    if (search_button) {
        invoke_or_click(search_button.value);
    } else {
        debug_log(L"search button not found; skip global Enter key");
    }
    sleep_short(800);
    debug_log(L"searched model query=" + clean_query);
    return true;
}

bool parameter_page_is_open(UiAutomation& automation, IUIAutomationElement* root) {
    return static_cast<bool>(find_descendant_contains(
        automation,
        root,
        {L"选型参数", L"产品名称", L"参数"},
        2200,
        {UIA_TabItemControlTypeId, UIA_TextControlTypeId, UIA_ButtonControlTypeId, UIA_EditControlTypeId}));
}

bool open_model_element(IUIAutomationElement* model) {
    if (!model) {
        return false;
    }
    const RECT rect = element_rect(model);
    if (!visible_screen_rect(rect)) {
        debug_element(L"skip offscreen model target", model);
        return false;
    }
    IUIAutomationInvokePattern* invoke = nullptr;
    if (SUCCEEDED(model->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke))) && invoke) {
        const HRESULT result = invoke->Invoke();
        invoke->Release();
        sleep_short(450);
        if (SUCCEEDED(result)) {
            debug_element(L"invoked model element", model);
            return true;
        }
    }
    if (select_item(model)) {
        debug_element(L"selected model element; no invoke pattern for open", model);
    }
    return false;
}

bool select_model_tree_element(IUIAutomationElement* model) {
    if (!model) {
        return false;
    }
    const RECT rect = element_rect(model);
    if (!visible_screen_rect(rect)) {
        debug_element(L"skip offscreen model tree target", model);
        return false;
    }
    if (select_item(model)) {
        debug_element(L"selected model tree element", model);
        IUIAutomationInvokePattern* invoke = nullptr;
        if (SUCCEEDED(model->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke))) && invoke) {
            const HRESULT result = invoke->Invoke();
            invoke->Release();
            sleep_short(450);
            if (SUCCEEDED(result)) {
                debug_element(L"opened selected model tree element", model);
            }
        }
        return true;
    }
    debug_element(L"model tree element has no SelectionItem pattern", model);
    return false;
}

bool open_path_or_model(UiAutomation& automation, IUIAutomationElement* root, const JiavuAutomationRequest& request) {
    if ((request.action == L"applyParams" || request.action == L"insert" || request.action == L"open") &&
        parameter_page_is_open(automation, root)) {
        debug_log(L"skip model search because parameter page is already open");
        return true;
    }

    const std::vector<RECT> excluded_rects = collect_cidu_rects(automation, root);
    UiElement scoped_panel = find_jiavu_panel_in_root(automation, root);
    if (scoped_panel) {
        root = scoped_panel.value;
        debug_element(L"open model scoped to jiavu panel", root);
    }

    select_custom_library(automation, root);
    sleep_short();

    const auto search_names = model_search_names(request);
    std::vector<std::wstring> parts = effective_model_path_parts(request);
    std::vector<std::wstring> leaf_names;
    if (parts.size() > 1) {
        if (contains_key(parts.front(), L"佳鱼参数化设计选型")) {
            parts.erase(parts.begin());
        }
        if (!parts.empty()) {
            leaf_names.push_back(parts.back());
            leaf_names.push_back(base_template_name(parts.back()));
        }
        for (std::size_t index = 0; index + 1 < parts.size(); ++index) {
            if (parts[index].find(L".jyasm") != std::wstring::npos || parts[index].find(L".jypar") != std::wstring::npos) {
                continue;
            }
            UiElement item = find_descendant_contains(
                automation,
                root,
                {parts[index]},
                2500,
                {UIA_TreeItemControlTypeId, UIA_ListItemControlTypeId, UIA_DataItemControlTypeId});
            if (item) {
                expand_or_click(item.value);
                sleep_short(220);
            }
        }
    }

    if (!leaf_names.empty()) {
        UiElement leaf = find_first_descendant(
            automation,
            root,
            [&](IUIAutomationElement* item) {
                if (inside_excluded_rects(item, excluded_rects) || looks_like_cidu_chat_block(element_name(item)) ||
                    likely_cidu_element_name(element_name(item))) {
                    return false;
                }
                const int type = element_control_type(item);
                if (type != UIA_TreeItemControlTypeId && type != UIA_ListItemControlTypeId &&
                    type != UIA_DataItemControlTypeId) {
                    return false;
                }
                if (!child_inside_parent(item, root)) {
                    return false;
                }
                return element_supports_tree_action(item) && model_candidate_matches(element_name(item), leaf_names);
            },
            4000);
        if (leaf) {
            if (request.action == L"selectModel") {
                return select_model_tree_element(leaf.value);
            }
            if (open_model_element(leaf.value) || select_model_tree_element(leaf.value)) {
                return true;
            }
        }
        debug_log(L"model leaf from full path not selected; fall back to model search names");
    }

    if (!search_names.empty()) {
        const std::wstring primary_query = model_leaf_search_query(request);
        search_model(automation, root, primary_query, excluded_rects);
    }

    UiElement model = find_first_descendant(
        automation,
        root,
        [&](IUIAutomationElement* item) {
            if (inside_excluded_rects(item, excluded_rects) || looks_like_cidu_chat_block(element_name(item)) ||
                likely_cidu_element_name(element_name(item))) {
                return false;
            }
            const int type = element_control_type(item);
            if (type != UIA_TreeItemControlTypeId && type != UIA_ListItemControlTypeId &&
                type != UIA_DataItemControlTypeId) {
                return false;
            }
            if (!child_inside_parent(item, root)) {
                return false;
            }
            return element_supports_tree_action(item) && model_candidate_matches(element_name(item), search_names);
        },
        3500);
    if (!model) {
        return false;
    }
    if (request.action == L"selectModel") {
        return select_model_tree_element(model.value);
    }
    return open_model_element(model.value);
}

bool click_named_button(UiAutomation& automation, IUIAutomationElement* root, const std::wstring& name) {
    UiElement button = find_descendant_contains(
        automation,
        root,
        {name},
        3000,
        {UIA_ButtonControlTypeId, UIA_TabItemControlTypeId, UIA_TextControlTypeId});
    return button && invoke_or_click(button.value);
}

UiElement find_edit_right_of_label(UiAutomation& automation, IUIAutomationElement* root, IUIAutomationElement* label) {
    const RECT label_rect = element_rect(label);
    if (!visible_screen_rect(label_rect) || !child_inside_parent(label, root)) {
        return {};
    }
    return find_first_descendant(
        automation,
        root,
        [&](IUIAutomationElement* item) {
            const int type = element_control_type(item);
            if (type != UIA_EditControlTypeId && type != UIA_ComboBoxControlTypeId &&
                type != UIA_DataItemControlTypeId && type != UIA_CustomControlTypeId) {
                return false;
            }
            const RECT edit_rect = element_rect(item);
            if (!visible_screen_rect(edit_rect) || !child_inside_parent(item, root)) {
                return false;
            }
            const int label_mid = (label_rect.top + label_rect.bottom) / 2;
            const int edit_mid = (edit_rect.top + edit_rect.bottom) / 2;
            return edit_rect.left >= label_rect.right - 10 &&
                   std::abs(edit_mid - label_mid) <= 14 &&
                   edit_rect.right > edit_rect.left;
        },
        3500);
}

int line_index_for_parameter_label(const std::wstring& text, const std::vector<std::wstring>& names) {
    const std::vector<std::wstring> lines = split_lines(text);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        for (const auto& name : names) {
            if (contains_key(lines[index], name)) {
                return static_cast<int>(index);
            }
        }
    }
    return -1;
}

UiElement find_value_control_by_row_index(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const RECT& table_text_rect,
    int row_index,
    int row_count,
    const std::vector<RECT>& excluded_rects) {
    if (!visible_screen_rect(table_text_rect) || row_index < 0 || row_count <= 0) {
        return {};
    }
    const double row_height = static_cast<double>(table_text_rect.bottom - table_text_rect.top) /
                              static_cast<double>(row_count);
    const int row_mid = table_text_rect.top + static_cast<int>((static_cast<double>(row_index) + 0.5) * row_height);
    int logged_candidates = 0;
    return find_first_descendant(
        automation,
        root,
        [&](IUIAutomationElement* item) {
            if (inside_excluded_rects(item, excluded_rects)) {
                return false;
            }
            const int type = element_control_type(item);
            if (type != UIA_EditControlTypeId && type != UIA_ComboBoxControlTypeId &&
                type != UIA_DataItemControlTypeId && type != UIA_CustomControlTypeId) {
                return false;
            }
            const RECT rect = element_rect(item);
            if (!visible_screen_rect(rect) || !element_overlaps_parent(item, root)) {
                return false;
            }
            const int mid = (rect.top + rect.bottom) / 2;
            const bool same_row = std::abs(mid - row_mid) <= 18;
            const bool right_side = rect.left > table_text_rect.left + 40;
            const bool small_enough = rect_width(rect) <= 900 && rect_height(rect) <= 80;
            if (same_row && right_side && logged_candidates < 8) {
                debug_element(L"row value candidate", item);
                ++logged_candidates;
            }
            return same_row && right_side && small_enough;
        },
        4500);
}

UiElement focused_value_element_for_row(
    UiAutomation& automation,
    IUIAutomationElement* root,
    int row_mid,
    const std::vector<RECT>& excluded_rects) {
    IUIAutomationElement* focused = nullptr;
    if (!automation || FAILED(automation.value->GetFocusedElement(&focused)) || !focused) {
        return {};
    }

    const RECT rect = element_rect(focused);
    const int mid = (rect.top + rect.bottom) / 2;
    const bool valid =
        visible_screen_rect(rect) &&
        !inside_excluded_rects(focused, excluded_rects) &&
        element_overlaps_parent(focused, root) &&
        std::abs(mid - row_mid) <= 30 &&
        (element_control_type(focused) == UIA_EditControlTypeId ||
         element_control_type(focused) == UIA_ComboBoxControlTypeId ||
         element_has_writable_value(focused));
    if (!valid) {
        debug_element(L"focused element rejected for parameter row", focused);
        focused->Release();
        return {};
    }

    debug_element(L"focused parameter value target", focused);
    return UiElement(focused);
}

UiElement activate_value_cell_by_row_geometry(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const RECT& table_text_rect,
    int row_index,
    int row_count,
    const std::vector<RECT>& excluded_rects) {
    (void)automation;
    (void)root;
    (void)table_text_rect;
    (void)row_index;
    (void)row_count;
    (void)excluded_rects;
    debug_log(L"skip parameter cell activation by geometry");
    return {};
}

UiElement find_value_control_from_multiline_table(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const std::vector<std::wstring>& names,
    const std::vector<RECT>& excluded_rects,
    bool allow_activate = false) {
    IUIAutomationCondition* condition = nullptr;
    IUIAutomationElementArray* elements = nullptr;
    if (!automation || !root || FAILED(automation.value->CreateTrueCondition(&condition)) || !condition) {
        return {};
    }
    const HRESULT found = root->FindAll(TreeScope_Subtree, condition, &elements);
    condition->Release();
    if (FAILED(found) || !elements) {
        return {};
    }

    int length = 0;
    elements->get_Length(&length);
    length = (length < 3500) ? length : 3500;
    for (int index = 0; index < length; ++index) {
        IUIAutomationElement* item = nullptr;
        if (FAILED(elements->GetElement(index, &item)) || !item) {
            continue;
        }
        if (inside_excluded_rects(item, excluded_rects) || element_control_type(item) != UIA_TextControlTypeId) {
            item->Release();
            continue;
        }
        const std::wstring text = element_name(item);
        if (text.find(L'\n') == std::wstring::npos) {
            item->Release();
            continue;
        }
        if (likely_cidu_element_name(text) || looks_like_cidu_chat_block(text)) {
            item->Release();
            continue;
        }
        const int row_index = line_index_for_parameter_label(text, names);
        if (row_index < 0) {
            item->Release();
            continue;
        }
        const std::vector<std::wstring> lines = split_lines(text);
        UiElement target = find_value_control_by_row_index(
            automation,
            root,
            element_rect(item),
            row_index,
            static_cast<int>(lines.size()),
            excluded_rects);
        if (!target && allow_activate) {
            target = activate_value_cell_by_row_geometry(
                automation,
                root,
                element_rect(item),
                row_index,
                static_cast<int>(lines.size()),
                excluded_rects);
        }
        item->Release();
        if (target) {
            elements->Release();
            return target;
        }
    }
    elements->Release();
    return {};
}

UiElement find_parameter_label(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const std::vector<std::wstring>& names,
    const std::vector<RECT>& excluded_rects) {
    const RECT root_rect = element_rect(root);
    return find_first_descendant(
        automation,
        root,
        [&](IUIAutomationElement* item) {
            if (inside_excluded_rects(item, excluded_rects) || !child_inside_parent(item, root)) {
                return false;
            }
            if (!mostly_in_right_side(element_rect(item), root_rect)) {
                return false;
            }
            const int type = element_control_type(item);
            if (type != UIA_TextControlTypeId && type != UIA_DataItemControlTypeId && type != UIA_CustomControlTypeId) {
                return false;
            }
            const std::wstring text = element_name(item);
            if (!looks_like_parameter_label_text(text)) {
                return false;
            }
            return std::any_of(names.begin(), names.end(), [&](const std::wstring& candidate) {
                return contains_key(text, candidate);
            });
        },
        3500);
}

void debug_parameter_controls(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const std::vector<RECT>& excluded_rects) {
    IUIAutomationCondition* condition = nullptr;
    IUIAutomationElementArray* elements = nullptr;
    if (!automation || !root || FAILED(automation.value->CreateTrueCondition(&condition)) || !condition) {
        return;
    }
    const HRESULT found = root->FindAll(TreeScope_Subtree, condition, &elements);
    condition->Release();
    if (FAILED(found) || !elements) {
        return;
    }
    int length = 0;
    elements->get_Length(&length);
    length = (length < 2500) ? length : 2500;
    int logged = 0;
    for (int index = 0; index < length && logged < 60; ++index) {
        IUIAutomationElement* item = nullptr;
        if (FAILED(elements->GetElement(index, &item)) || !item) {
            continue;
        }
        const int type = element_control_type(item);
        const std::wstring name = element_name(item);
        const bool interesting_type =
            type == UIA_TextControlTypeId || type == UIA_EditControlTypeId ||
            type == UIA_ComboBoxControlTypeId || type == UIA_DataItemControlTypeId ||
            type == UIA_CustomControlTypeId || type == UIA_ButtonControlTypeId;
        const bool interesting_name =
            contains_key(name, L"斜梯") || contains_key(name, L"参数") || contains_key(name, L"WT") ||
            contains_key(name, L"物料") || contains_key(name, L"插入") || contains_key(name, L"打开") ||
            contains_key(name, L"800") || contains_key(name, L"1000") || contains_key(name, L"100") ||
            contains_key(name, L"有") || contains_key(name, L"30");
        if (interesting_type && interesting_name && !inside_excluded_rects(item, excluded_rects) &&
            visible_screen_rect(element_rect(item))) {
            debug_element(L"parameter debug candidate", item);
            ++logged;
        }
        item->Release();
    }
    elements->Release();
}

ParameterApplyResult apply_parameters(
    UiAutomation& automation,
    IUIAutomationElement* root,
    const JiavuAutomationRequest& request,
    const std::vector<RECT>& excluded_rects = {}) {
    ParameterApplyResult result;
    const std::vector<OrderedParameter> ordered_parameters = ordered_parameters_from_request(request);
    result.requested = writable_parameter_count(ordered_parameters);
    if (ordered_parameters.empty()) {
        return result;
    }
    UiElement panel = find_jiavu_panel_in_root(automation, root);
    IUIAutomationElement* parameter_root = panel ? panel.value : nullptr;
    if (!parameter_root && root && contains_key(element_name(root), L"佳鱼参数化设计选型")) {
        parameter_root = root;
        debug_log(L"use parameter root by jiavu window name");
    }
    if (!parameter_root && root) {
        JiavuMarkers markers = detect_jiavu_markers(automation, root, excluded_rects);
        debug_log(L"fallback parameter root markers: " + marker_text(markers));
        if (markers.parameter_tab || markers.parameter_grid || markers.insert_button || markers.open_button) {
            parameter_root = root;
        }
    }
    if (!parameter_root) {
        debug_log(L"refuse to apply parameters without a scoped Jiavu panel");
        return result;
    }
    root = parameter_root;
    debug_element(L"use parameter root", root);
    debug_parameter_controls(automation, root, excluded_rects);

    ParameterApplyResult accessible_result = apply_parameters_by_accessible_rows(
        automation,
        root,
        ordered_parameters,
        excluded_rects);
    if (accessible_result.applied > 0 && accessible_result.page_refresh_confirmed) {
        return accessible_result;
    }
    if (accessible_result.applied > 0) {
        debug_log(
            L"UIA row write applied but display not confirmed; try visible controls");
    }

    ParameterApplyResult visible_result = apply_parameters_by_visible_controls(
        automation,
        root,
        ordered_parameters,
        excluded_rects);
    if (visible_result.applied > 0 && visible_result.page_refresh_confirmed) {
        return visible_result;
    }
    if (visible_result.applied > 0) {
        debug_log(
            L"visible control write applied but display not fully confirmed; try DataParser object");
    }

    ParameterApplyResult data_parser_result = apply_parameters_by_data_parser(ordered_parameters);
    if (data_parser_result.applied > 0) {
        if (refresh_parameter_tab_by_accessible_actions(automation, root, excluded_rects)) {
            data_parser_result.displayed = confirm_displayed_parameters(
                automation,
                root,
                ordered_parameters,
                excluded_rects);
            data_parser_result.page_refresh_confirmed =
                data_parser_result.displayed >= data_parser_result.applied;
        }
        if (data_parser_result.page_refresh_confirmed) {
            return data_parser_result;
        }
        debug_log(L"DataParser write display not confirmed; fall back to DataProp objects");
    }

    debug_log(
        L"skip DataProp backend object write: previous runs changed hidden/stale CDataItemProp objects "
        L"without updating the visible DuiLib parameter list");
    return data_parser_result.applied > 0 ? data_parser_result : result;

#if 0
    const QtDirectApi qt_api = qt_direct_api();
    if (!qt_api.widget_write_ok()) {
        debug_log(L"Qt direct API unavailable");
    }
    debug_qt_widget_surface(qt_api, element_rect(root), excluded_rects);
    ParameterApplyResult qt_tree_result = apply_parameters_by_qt_tree(
        qt_api,
        element_rect(root),
        ordered_parameters,
        excluded_rects);
    if (qt_tree_result.applied > 0) {
        return qt_tree_result;
    }
    ParameterApplyResult qt_table_result = apply_parameters_by_qt_table(
        qt_api,
        element_rect(root),
        ordered_parameters,
        excluded_rects);
    if (qt_table_result.applied > 0) {
        return qt_table_result;
    }
    ParameterApplyResult qt_editor_result = apply_parameters_by_qt_editors(
        qt_api,
        element_rect(root),
        ordered_parameters,
        excluded_rects);
    if (qt_editor_result.applied > 0) {
        return qt_editor_result;
    }

    for (const auto& parameter : ordered_parameters) {
        const std::wstring& name = parameter.name;
        const std::wstring& value = parameter.value;
        if (trim(value).empty() || contains_key(name, L"关键词")) {
            continue;
        }
        ++result.requested;
        std::vector<std::wstring> names = {name};
        const auto unit_pos = name.find(L'[');
        if (unit_pos != std::wstring::npos) {
            names.push_back(name.substr(0, unit_pos));
        }
        UiElement label = find_parameter_label(automation, root, names, excluded_rects);
        if (label) {
            debug_element(L"parameter label", label.value);
            UiElement edit = find_value_control_from_multiline_table(automation, root, names, excluded_rects);
            if (!edit) {
                edit = find_edit_right_of_label(automation, root, label.value);
            }
            if (edit) {
                debug_element(L"parameter value target", edit.value);
            }
            if (edit && element_control_type(edit.value) == UIA_ComboBoxControlTypeId &&
                set_combo_value(automation, edit.value, value)) {
                ++result.applied;
                continue;
            }
            if (edit && set_edit_value(edit.value, value)) {
                ++result.applied;
                continue;
            }
            if (set_qt_value_right_of_label(qt_api, element_rect(root), element_rect(label.value), value)) {
                ++result.applied;
                continue;
            }
            debug_log(L"no writable value target for parameter=" + name);
        } else {
            UiElement edit = find_value_control_from_multiline_table(automation, root, names, excluded_rects);
            if (edit) {
                debug_element(L"parameter value target", edit.value);
                if (element_control_type(edit.value) == UIA_ComboBoxControlTypeId &&
                    set_combo_value(automation, edit.value, value)) {
                    ++result.applied;
                    continue;
                }
                if (set_edit_value(edit.value, value)) {
                    ++result.applied;
                    continue;
                }
            }
            debug_log(L"parameter label not found=" + name);
        }
    }
    return result;
#endif
}

JiavuAutomationResult run_windows_automation(const JiavuAutomationRequest& request) {
    debug_log(L"run_windows_automation build=2026-05-15-generate-model-button action=" + request.action);
    UiAutomation automation;
    if (!automation) {
        return {false, L"无法初始化 Windows UI Automation。"};
    }

    UiElement root = find_jiavu_root(automation);
    if (!root) {
        return {false, L"未找到“佳鱼参数化设计选型”窗口，请先打开佳鱼参数化设计选型插件。"};
    }

    const bool should_locate_model =
        (request.action == L"selectModel" || request.action == L"applyParams") &&
        (!request.selected_model.empty() || !request.template_name.empty() || !request.full_node_path.empty());
    if (should_locate_model) {
        if (!open_path_or_model(automation, root.value, request)) {
            return {false, L"未能在自定义库中定位到确认的模型，请确认佳鱼树节点已加载或模型路径可见。"};
        }
        sleep_short(450);
    }

    ParameterApplyResult apply_result;
    if (request.action == L"applyParams" || request.action == L"insert" || request.action == L"open") {
        apply_result = apply_parameters(automation, root.value, request, collect_cidu_rects(automation, root.value));
        if (apply_result.requested > 0 && apply_result.applied == 0) {
            return {
                false,
                L"未写入参数：没有定位到可直接写入的佳鱼参数对象。"
                L"本次未使用鼠标、键盘、剪贴板或坐标点击。"};
        }
    }

    if (request.action == L"insert") {
        if (!click_named_button(automation, root.value, L"插入")) {
            return {false, L"未执行插入：没有找到可通过控件接口调用的“插入”按钮。"};
        }
    } else if (request.action == L"open") {
        if (!click_named_button(automation, root.value, L"打开")) {
            return {false, L"未执行打开：没有找到可通过控件接口调用的“打开”按钮。"};
        }
    }

    std::wostringstream message;
    bool ok = true;
    if (request.action == L"selectModel") {
        message << L"已尝试在自定义库中选中并打开模型参数页。";
    } else {
        if (apply_result.backend_only && !apply_result.page_refresh_confirmed) {
            ok = false;
            message << L"未应用到佳鱼参数页：已尝试更新后台参数对象 " << apply_result.applied
                    << L" 项，但页面显示未确认变化。当前版本不会执行鼠标键盘模拟、坐标点击或易导致 ZW3D 崩溃的强制窗口刷新。";
        } else {
            message << L"已通过佳鱼参数对象应用参数 " << apply_result.applied << L" 项";
        }
        if (request.action == L"insert") {
            message << L"，并点击“插入”。";
        } else if (request.action == L"open") {
            message << L"，并点击“打开”。";
        } else {
            message << L"。";
        }
    }
    return {ok, message.str()};
}

#endif

}  // namespace

JiavuAutomationResult run_jiavu_automation(const JiavuAutomationRequest& request) {
#if defined(_WIN32)
    return run_windows_automation(request);
#else
    (void)request;
    return {false, L"佳鱼参数化设计选型自动操作只支持 Windows。"};
#endif
}

}  // namespace cidu_zw3d
