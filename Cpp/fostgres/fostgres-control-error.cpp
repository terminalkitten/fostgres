/*
    Copyright 2019, Felspar Co Ltd. http://support.felspar.com/
    Distributed under the Boost Software License, Version 1.0.
    See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt
*/
#include <pqxx/except>

#include <fost/insert>
#include <fost/push_back>

#include <fostgres/matcher.hpp>
#include <fostgres/response.hpp>


namespace {


    const class fostgres_control_error : public fostlib::urlhandler::view {
      public:
        fostgres_control_error() : view("fostgres.control.pg-error") {}

        std::pair<boost::shared_ptr<fostlib::mime>, int> operator()(
                const fostlib::json &config,
                const fostlib::string &path,
                fostlib::http::server::request &req,
                const fostlib::host &host) const {
            try {
                return execute(config["execute"], path, req, host);
            } catch (const pqxx::pqxx_exception &e) {
                return execute(config[""], path, req, host);
            }
        }

    } c_fostgres_control_error;


}
