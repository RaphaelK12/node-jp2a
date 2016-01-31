#include "jp2a-1.0.6/config.h"
#include "jp2a-1.0.6/include/jp2a.h"
#include "jp2a-1.0.6/include/options.h"
#include "Image.h"

#include <fstream>
#include <node.h>
#include <node_object_wrap.h>

using v8::Boolean;
using v8::Exception;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Function;
using v8::Handle;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::Persistent;
using v8::String;
using v8::Value;

void processJPEG(Isolate *isolate, const char *filename,
                 std::stringstream &ss) {
  std::ifstream is(filename, std::ifstream::binary);
  if (!is) {
    isolate->ThrowException(
        Exception::TypeError(String::NewFromUtf8(isolate, "Wrong arguments")));
    return;
  }
  is.seekg(0, is.end);
  int length = is.tellg();
  is.seekg(0, is.beg);
  char *buffer = new char[length];
  is.read(buffer, length);
  is.close();
  JP2A::Image image{};
  if (!image.init(reinterpret_cast<unsigned char *>(buffer), length)) {
    isolate->ThrowException(Exception::Error(
        String::NewFromUtf8(isolate, image.errorMessage().c_str())));
    delete[] buffer;
    return;
  }
  if (!image.alloc()) {
    isolate->ThrowException(Exception::Error(
        String::NewFromUtf8(isolate, image.errorMessage().c_str())));
    delete[] buffer;
    return;
  }
  image.process();
  image >> ss;
}

void Jp2a(const FunctionCallbackInfo<Value> &arguments) {
  Isolate *isolate = arguments.GetIsolate();
  if (arguments.IsConstructCall()) {
    Local<Function> fn = FunctionTemplate::New(isolate, Jp2a)->GetFunction();
    fn->SetName(String::NewFromUtf8(isolate, "jp2a"));
    return arguments.GetReturnValue().Set(fn);
  }
  if (arguments.Length() < 1 || !arguments[0]->IsString()) {
    isolate->ThrowException(
        Exception::TypeError(String::NewFromUtf8(isolate, "Wrong arguments")));
    return;
  }
  init_options(); // TODO: handle options

  String::Utf8Value filename(arguments[0]->ToString());
  std::stringstream ss;
  processJPEG(isolate, *filename, ss);
  arguments.GetReturnValue().Set(
      String::NewFromUtf8(isolate, ss.str().c_str()));
}

class ImageWrap : public node::ObjectWrap {
public:
  static void Init(Local<Object>);
  JP2A::Image *i;

private:
  // construction
  static Persistent<Function> constructor;
  static void New(const FunctionCallbackInfo<Value> &);
  // prototype
  static void Close(const FunctionCallbackInfo<Value> &);
  static void Info(const FunctionCallbackInfo<Value> &);
  static void Decode(const FunctionCallbackInfo<Value> &);

  explicit ImageWrap() {}
  ~ImageWrap() {}
};

Persistent<Function> ImageWrap::constructor;

void ImageWrap::Init(Local<Object> exports) {
  Isolate *isolate = exports->GetIsolate();
  Local<FunctionTemplate> tpl = FunctionTemplate::New(isolate, New);
  Local<String> imageString = String::NewFromUtf8(isolate, "Image");
  tpl->SetClassName(imageString);
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  NODE_SET_PROTOTYPE_METHOD(tpl, "close", Close);
  NODE_SET_PROTOTYPE_METHOD(tpl, "info", Info);
  NODE_SET_PROTOTYPE_METHOD(tpl, "decode", Decode);

  constructor.Reset(isolate, tpl->GetFunction());
  exports->ForceSet(imageString, tpl->GetFunction(), v8::ReadOnly);
}

void ImageWrap::Decode(const FunctionCallbackInfo<Value> &arguments) {
  Isolate *isolate = arguments.GetIsolate();
  JP2A::Image *image = ObjectWrap::Unwrap<ImageWrap>(arguments.Holder())->i;
  if (!image) {
    isolate->ThrowException(Exception::ReferenceError(
        String::NewFromUtf8(isolate, "Image is closed")));
    return;
  }
  std::stringstream ss;
  (*image) >> ss;
  Local<String> ret = String::NewFromUtf8(isolate, ss.str().c_str());
  if (arguments.Length() < 1) {
    return arguments.GetReturnValue().Set(ret);
  }
  if (arguments[0]->IsFunction()) {
    Local<Value> object[] = {ret};
    Local<Function>::Cast(arguments[0])
        ->Call(isolate->GetCurrentContext()->Global(), 1, object);
  }
  return arguments.GetReturnValue().Set(arguments.This());
}

void ImageWrap::Info(const FunctionCallbackInfo<Value> &arguments) {
  Isolate *isolate = arguments.GetIsolate();
  JP2A::Image *image = ObjectWrap::Unwrap<ImageWrap>(arguments.Holder())->i;
  if (!image) {
    isolate->ThrowException(Exception::ReferenceError(
        String::NewFromUtf8(isolate, "Image is closed")));
    return;
  }
  Local<Object> object = Object::New(isolate);
  object->Set(String::NewFromUtf8(isolate, "source_width"),
              Integer::New(isolate, image->jpg()->output_width));
  object->Set(String::NewFromUtf8(isolate, "source_height"),
              Integer::New(isolate, image->jpg()->output_height));
  object->Set(String::NewFromUtf8(isolate, "output_components"),
              Integer::New(isolate, image->jpg()->output_components));
  object->Set(String::NewFromUtf8(isolate, "output_width"),
              Integer::New(isolate, image->width()));
  object->Set(String::NewFromUtf8(isolate, "output_height"),
              Integer::New(isolate, image->height()));
  return arguments.GetReturnValue().Set(object);
}

void ImageWrap::Close(const FunctionCallbackInfo<Value> &arguments) {
  Isolate *isolate = arguments.GetIsolate();
  ImageWrap *image = ObjectWrap::Unwrap<ImageWrap>(arguments.Holder());
  if (image->i) {
    delete image->i;
    image->i = nullptr;
    return arguments.GetReturnValue().Set(Boolean::New(isolate, true));
  }
  arguments.GetReturnValue().Set(Boolean::New(isolate, false));
}

void ImageWrap::New(const FunctionCallbackInfo<Value> &arguments) {
  Isolate *isolate = arguments.GetIsolate();
  if (arguments.IsConstructCall()) {
    if (arguments.Length() < 1 || !arguments[0]->IsString()) {
      isolate->ThrowException(Exception::TypeError(
          String::NewFromUtf8(isolate, "Wrong arguments")));
      return;
    }
    String::Utf8Value filename(arguments[0]->ToString());
    std::ifstream is(*filename, std::ifstream::binary);
    if (!is) {
      isolate->ThrowException(Exception::TypeError(
          String::NewFromUtf8(isolate, "Wrong arguments")));
      return;
    }
    is.seekg(0, is.end);
    int length = is.tellg();
    is.seekg(0, is.beg);
    char *buffer = new char[length];
    is.read(buffer, length);
    is.close();
    auto *i = new JP2A::Image{};
    if (!i->init(reinterpret_cast<unsigned char *>(buffer), length)) {
      isolate->ThrowException(Exception::Error(
          String::NewFromUtf8(isolate, i->errorMessage().c_str())));
      delete[] buffer;
      return;
    }
    if (!i->alloc()) {
      isolate->ThrowException(Exception::Error(
          String::NewFromUtf8(isolate, i->errorMessage().c_str())));
      delete[] buffer;
      return;
    }
    i->process();
    ImageWrap *image = new ImageWrap();
    image->i = i;
    image->Wrap(arguments.This());
    arguments.GetReturnValue().Set(arguments.This());
  } else {
    Local<Value> argv[] = {arguments[0]};
    Local<Function> cons = Local<Function>::New(isolate, constructor);
    arguments.GetReturnValue().Set(cons->NewInstance(1, argv));
  }
}

void Init(Handle<Object> exports, Handle<Object> module) {
  Isolate *isolate = exports->GetIsolate();
  Local<Function> fn = FunctionTemplate::New(isolate, Jp2a)->GetFunction();
  Local<String> jp2a = String::NewFromUtf8(isolate, "jp2a");
  fn->SetName(jp2a);
  fn->ForceSet(String::NewFromUtf8(isolate, "version"),
               String::NewFromUtf8(isolate, VERSION), v8::ReadOnly);
  ImageWrap::Init(fn);
  module->Set(String::NewFromUtf8(isolate, "exports"), fn);
}

NODE_MODULE(jp2a, Init)
