#pragma once
namespace opennow {
enum LogLevel { LogTrace, LogDebug, LogInfo, LogWarn, LogError, LogFatal };
bool log_init(); void log_close(); const char* log_path();
#if defined(__GNUC__) || defined(__clang__)
void log_message(LogLevel level,const char* component,const char* format,...) __attribute__((format(printf,3,4)));
#else
void log_message(LogLevel level,const char* component,const char* format,...);
#endif
}
#define ON_LOGT(component, ...) ::opennow::log_message(::opennow::LogTrace, component, __VA_ARGS__)
#define ON_LOGD(component, ...) ::opennow::log_message(::opennow::LogDebug, component, __VA_ARGS__)
#define ON_LOGI(component, ...) ::opennow::log_message(::opennow::LogInfo, component, __VA_ARGS__)
#define ON_LOGW(component, ...) ::opennow::log_message(::opennow::LogWarn, component, __VA_ARGS__)
#define ON_LOGE(component, ...) ::opennow::log_message(::opennow::LogError, component, __VA_ARGS__)
#define ON_LOGF(component, ...) ::opennow::log_message(::opennow::LogFatal, component, __VA_ARGS__)
