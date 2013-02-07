#include <sstream>

#include "JsEngine.h"

AdblockPlus::JsEngine::JsEngine() : context(v8::Context::New())
{
}

void AdblockPlus::JsEngine::Evaluate(const std::string& source)
{
  v8::HandleScope handleScope;
  v8::Context::Scope contextScope(context);
  v8::Handle<v8::String> v8Source = v8::String::New(source.c_str());
  v8::Handle<v8::Script> script = v8::Script::Compile(v8Source);
  script->Run();
}

std::string AdblockPlus::JsEngine::Call(const std::string& functionName)
{
  v8::HandleScope handleScope;
  v8::Context::Scope contextScope(context);
  v8::Local<v8::Object> global = context->Global();
  v8::Local<v8::Function> function = v8::Local<v8::Function>::Cast(
    global->Get(v8::String::New(functionName.c_str())));
  v8::Local<v8::Value> result = function->Call(function, 0, 0);
  v8::String::AsciiValue ascii(result);
  return *ascii;
}