#pragma once
#ifndef CATA_SRC_CATA_SCOPE_EXIT_H
#define CATA_SRC_CATA_SCOPE_EXIT_H

#include <functional>

struct cata_scope_exit {
        template<typename OnExit>
        cata_scope_exit( OnExit &&cb ) : cb_( std::move( cb ) ) {}
        ~cata_scope_exit() {
            if( cb_ ) {
                cb_();
            }
        }

        void dismiss() {
            cb_ = nullptr;
        }

    private:
        std::function<void()> cb_;
};

#endif
