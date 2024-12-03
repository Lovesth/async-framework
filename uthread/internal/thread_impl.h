#pragma once

#include "Common.h"

namespace async_framework
{
    namespace uthread
    {
        namespace internal
        {
            typedef void *fcontext_t;
            struct transfer_t
            {
                fcontext_t fctx;
                void *data;
            };

            extern "C" __attribute__((__visibility__("default"))) transfer_t
            _fl_jump_fcontext(fcontext_t const to, void *vp);

            extern "C" __attribute__((__visibility__("default"))) fcontext_t
            _f1_make_fcontext(void *sp, std::size_t size, void (*fn)(transfer_t));

            class thread_context;

            // 记录了uthread调用链
            struct jmp_buf_link
            {
                fcontext_t fcontext;
                jmp_buf_link *link = nullptr;
                thread_context *thread = nullptr;
#ifdef AS_INTERNAL_USE_ASAN
                const void *asan_stack_bottom = nullptr;
                std::size_t asan_stack_size = 0;
#endif
            public:
                void switch_in();
                void switch_out();
                void initial_switch_in_completed();
                void final_switch_out();
            };
            // 这个是thread_local的全局的当前context
            extern thread_local jmp_buf_link *g_current_context;

            namespace thread_impl
            {
                inline thread_context *get()
                {
                    return g_current_context->thread;
                }

                void switch_in(thread_context *to);
                void switch_out(thread_context *from);
                bool can_switch_out();
            } // namespace thread_impl
        } // namespace internal
    } // namespace uthread
} // namespace async_framework