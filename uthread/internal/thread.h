#pragma once
#include <memory>
#include "Future.h"
#include "Promise.h"
#include "thread_impl.h"

namespace async_framework
{
    namespace uthread
    {
        namespace internal
        {
            // default 0.5MB
            inline constexpr size_t default_base_stack_size = 512 * 1024;
            size_t get_base_stack_size();

            class thread_context
            {
                struct stack_deleter
                {
                    void operator()(char *ptr) const noexcept;
                };

                using stack_holder = std::unique_ptr<char[], stack_deleter>;

                const size_t stack_size_;
                stack_holder stack_{make_stack()};
                std::function<void()> func_;
                jmp_buf_link context_;

            public:
                bool joined_ = false;
                Promise<bool> done_;

            private:
                static void s_main(transfer_t t);
                void setup();
                void main();
                stack_holder make_stack();

            public:
                explicit thread_context(std::function<void()> func, size_t stack_size = 0);
                ~thread_context();

                void switch_in();
                void switch_out();
                friend void thread_impl::switch_in(thread_context *);
                friend void thread_impl::switch_out(thread_context *);
            };
        }
    } // namespace uthread
} // namespace async_framework