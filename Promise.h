#pragma once

#include <exception>
#include "Common.h"
#include "Future.h"

namespace async_framework
{
    template <typename T>
    class Future;

    // The well-known Future/Promise pair mimics a producer/consumer pair.
    // The Promise stands for the producer-side.
    //
    // We could get a Future from the Promise by calling getFuture(). And
    // set value by calling setValue(). In case we need to set exception,
    // we could call setException().
    template <typename T>
    class Promise
    {
    public:
        using value_type = std::conditional_t<std::is_void_v<T>, Unit, T>;
        Promise() : sharedState_(new FutureState<value_type>()), hasFuture_(false)
        {
            sharedState_->attachPromise();
        }
        ~Promise()
        {
            if (sharedState_)
            {
                sharedState_->detachPromise();
            }
        }

        // 拷贝构造函数
        Promise(const Promise &other)
        {
            sharedState_ = other.sharedState_;
            hasFuture_ = other.hasFuture_;
            sharedState_->attachPromise();
        }

        Promise &operator=(const Promise &other)
        {
            if (this == &other)
            {
                return *this;
            }
            this->~Promise();
            sharedState_ = other.sharedState_;
            hasFuture_ = other.hasFuture_;
            sharedState_->attachPromise();
            return *this;
        }

        Promise(Promise<T> &&other) : sharedState_(std::exchange(other.sharedState_, nullptr)), hasFuture_(std::exchange(other.hasFuture_, nullptr))
        {
        }

        Promise &operator=(Promise<T> &&other)
        {
            std::swap(sharedState_, other.sharedState_);
            std::swap(hasFuture_, other.hasFuture_);
            return *this;
        }

    public:
        Future<T> getFuture()
        {
            logicAssert(valid(), "Promise is broken");
            logicAssert(!hasFuture_, "Promise already has a future");

            hasFuture_ = true;
            return Future<T>(sharedState_);
        }

        bool valid() const
        {
            return sharedState_ != nullptr;
        }

        // make the continuation back to origin context
        Promise &checkout()
        {
            if (sharedState_)
            {
                sharedState_->checkOut();
            }
            return *this;
        }

        Promise &forceSched()
        {
            if (sharedState_)
            {
                sharedState_->setForceSched();
            }
            return *this;
        }

    public:
        void setException(std::exception_ptr error)
        {
            logicAssert(valid(), "Promise is broken");
            sharedState_->setResult(Try<value_type>(error));
        }

        void setValue(value_type &&v)
            requires(!std::is_void_v<T>)
        {
            logicAssert(valid(), "Promise is broken");
            sharedState_->setResult(Try<value_type>(std::forward<T>(v)));
        }

        void setValue(Try<value_type> &&t)
        {
            logicAssert(valid(), "Promise is broken");
            sharedState_->setResult(std::move(t));
        }

        void setValue()
            requires(std::is_void_v<T>)
        {
            logicAssert(valid(), "Promise is broken");
            sharedState_->setResult(Try<value_type>(Unit()));
        }

    private:
        FutureState<value_type> *sharedState_ = nullptr;
        bool hasFuture_ = false;
    };
} // namespace async_framework