// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "ds/buffer.h"
#include "ds/logger.h"

#include <memory>
#include <quickjs/quickjs-atom.h>
#include <quickjs/quickjs.h>
#include <sstream>
#include <vector>

namespace ccfapp
{
  using namespace std;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"

  JSValue js_print(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
  {
    int i;
    const char* str;
    std::stringstream ss;

    for (i = 0; i < argc; i++)
    {
      if (i != 0)
        ss << ' ';
      if (!JS_IsError(ctx, argv[i]) && JS_IsObject(argv[i]))
      {
        JSValue rval = JS_JSONStringify(ctx, argv[i], JS_NULL, JS_NULL);
        str = JS_ToCString(ctx, rval);
        JS_FreeValue(ctx, rval);
      }
      else
        str = JS_ToCString(ctx, argv[i]);
      if (!str)
        return JS_EXCEPTION;
      ss << str;
      JS_FreeCString(ctx, str);
    }
    LOG_INFO << ss.str() << std::endl;
    return JS_UNDEFINED;
  }

  JSValue js_dump_error(JSContext* ctx)
  {
    JSValue exception_val = JS_GetException(ctx);

    JSValue val;
    const char* stack;
    bool is_error;

    is_error = JS_IsError(ctx, exception_val);
    if (!is_error)
      LOG_INFO_FMT("Throw: ");
    js_print(ctx, JS_NULL, 1, (JSValueConst*)&exception_val);
    if (is_error)
    {
      val = JS_GetPropertyStr(ctx, exception_val, "stack");
      if (!JS_IsUndefined(val))
      {
        stack = JS_ToCString(ctx, val);
        LOG_INFO_FMT("{}", stack);

        JS_FreeCString(ctx, stack);
      }
      JS_FreeValue(ctx, val);
    }

    JS_FreeValue(ctx, exception_val);

    return JS_EXCEPTION;
  }

  std::optional<std::vector<std::string>> js_get_string_array(JSContext* ctx, JSValue obj, const std::string& arg_name_for_error = "")
  {
    if (!JS_IsArray(ctx, obj))
    {
      if (!arg_name_for_error.empty())
        JS_ThrowTypeError(ctx, "%s must be an array", arg_name_for_error);
      return std::nullopt;
    }
    int64_t length = js_get_int_property(ctx, obj, "length");
    if (length == -1)
      return std::nullopt;
    std::vector<std::string> items;
    for (uint32_t i=0; i < length; i++)
    {
      JSValue item = JS_GetPropertyUint32(ctx, obj, i);
      auto str = js_get_string(ctx, item);
      JS_FreeValue(ctx, item);
      if (!str.has_value())
        return std::nullopt;
      items.emplace_back(std::move(str.value()));
    }
    return items;
  }

  // Access the raw buffer of an ArrayBuffer or TypedArray.
  Buffer js_get_array_buffer(JSContext* ctx, JSValueConst arg, const std::string& arg_name_for_error = "")
  {
    size_t buf_size;
    size_t buf_offset;
    JSValue typed_array_buffer =
      JS_GetTypedArrayBuffer(ctx, arg, &buf_offset, &buf_size, nullptr);

    uint8_t* array_buffer;
    if (!JS_IsException(typed_array_buffer))
    {
      size_t buf_size_total;
      array_buffer =
        JS_GetArrayBuffer(ctx, &buf_size_total, typed_array_buffer);
      JS_FreeValue(ctx, typed_array_buffer);
      if (!array_buffer)
        return Buffer();
      array_buffer += buf_offset;
    }
    else
      array_buffer = JS_GetArrayBuffer(ctx, &buf_size, arg);

    if (!array_buffer)
    {
      JS_ThrowTypeError(ctx, "%s must be an ArrayBuffer or TypedArray", arg_name_for_error);
      return Buffer();
    }

    return Buffer(array_buffer, buf_size);
  }

  std::optional<std::string> js_get_string(
    JSContext* ctx, JSValue arg, const std::string& arg_name_for_error = "")
  {
    if (!JS_IsString(arg))
    {
      if (!arg_name_for_error.empty())
        JS_ThrowTypeError(ctx, "%s must be a string", arg_name_for_error);
      return std::nullopt;
    }
    const char* cstr = JS_ToCString(ctx, arg);
    if (!cstr)
      return std::nullopt;
    std::string str(cstr);
    JS_FreeCString(ctx, cstr);
    return str;
  }

  int js_get_bool(
    JSContext* ctx, JSValue arg, const std::string& arg_name_for_error = "")
  {
    int b = JS_ToBool(ctx, arg);
    if (b == -1 && !arg_name_for_error.empty())
      JS_ThrowTypeError(ctx, "%s must be convertible to a boolean", arg_name_for_error);
    return b;
  }

  std::optional<std::string> js_get_string_property(
    JSContext* ctx, JSValue obj, const std::string& name)
  {
    JSValue str_val = JS_GetPropertyStr(ctx, obj, name.c_str());
    if (JS_IsException(str_val))
      return std::nullopt;
    auto str = js_get_string(ctx, str_val, name);
    JS_FreeValue(ctx, str_val);
    return str;
  }

  int64_t js_get_int_property(
    JSContext* ctx, JSValue obj, const std::string& name)
  {
    JSValue val = JS_GetPropertyStr(ctx, obj, name.c_str());
    if (JS_IsException(val))
      return -1;
    int64_t i;
    int res = JS_ToInt64(ctx, &i, val);
    JS_FreeValue(ctx, val);
    if (res == -1)
      return -1;
    return i;
  }

  Buffer js_get_array_buffer_property(
    JSContext* ctx, JSValue obj, const std::string& name)
  {
    JSValue buf_val = JS_GetPropertyStr(ctx, obj, name.c_str());
    if (JS_IsException(buf_val))
      return Buffer();
    auto buf = js_get_array_buffer(ctx, buf_val);
    JS_FreeValue(ctx, buf_val);
    return buf;
  }

#pragma clang diagnostic pop
}
