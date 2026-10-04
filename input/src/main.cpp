#include <stdint.h>

#include <rawline/input_module.hpp>

namespace {

rawline::input_module::Context service_context;

void input_worker(void* argument)
{
    const auto* context = static_cast<const rawline::input_module::Context*>(argument);
    bool logged_packet = false;
    for (;;) {
        rawline::pointer::Event event{};
        if (!context->api->poll_pointer_event(&event)) {
            context->api->wait_for_interrupt();
            continue;
        }
        if (!logged_packet) {
            logged_packet = true;
            context->api->write_serial("RAWLINE input first pointer packet consumed\r\n");
        }
        if (!context->api->dispatch_pointer_event(&event)) continue;
    }
}

}

extern "C" int64_t input_entry(const rawline::input_module::Context* context)
{
    if (context == nullptr || context->version != rawline::input_module::context_version ||
        context->size < sizeof(rawline::input_module::Context) || context->api == nullptr ||
        context->api->version != rawline::input_module::api_version ||
        context->api->size < sizeof(rawline::input_module::Api) ||
        context->api->poll_pointer_event == nullptr ||
        context->api->dispatch_pointer_event == nullptr ||
        context->api->create_thread == nullptr || context->api->write_serial == nullptr ||
        context->api->wait_for_interrupt == nullptr) return -1;

    service_context.version = context->version;
    service_context.size = context->size;
    service_context.api = context->api;
    if (!context->api->create_thread(input_worker, &service_context)) return -1;
    context->api->write_serial("RAWLINE input.rwl entered\r\n");
    return 0;
}
