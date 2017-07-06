/*
    Copyright 2016-2017, Felspar Co Ltd. http://support.felspar.com/
    Distributed under the Boost Software License, Version 1.0.
    See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt
*/


#pragma once


#include <fost/urlhandler>
#include <fost/postgres>


namespace fostgres {


    /// A MIME type that provides CSJ data
    class csj_mime : public fostlib::mime {
        enum class output { csj, csv } format;
        mutable bool done = false;
        mutable std::vector<fostlib::string> columns;
        mutable fostlib::pg::recordset rs;

    public:
        class csj_iterator : public fostlib::mime::iterator_implementation {
            friend class csj_mime;

            const csj_mime::output format;
            fostlib::pg::recordset rs;
            fostlib::pg::recordset::const_iterator iter, end;
            std::string current;
            bool sent_first = false;

            csj_iterator(csj_mime::output format,
                std::vector<fostlib::string> &&columns, fostlib::pg::recordset &&r);

            void line();

        public:
            fostlib::const_memory_block operator () ();
        };

        csj_mime(
            const fostlib::string &accept,
            std::vector<fostlib::string> &&cols,
            fostlib::pg::recordset &&rs);

        std::unique_ptr<iterator_implementation> iterator() const;

        bool boundary_is_ok(const fostlib::string &) const {
            throw fostlib::exceptions::not_implemented(__func__);
        }
        std::ostream &print_on(std::ostream &) const {
            throw fostlib::exceptions::not_implemented(__func__);
        }
    };


    /// A MIME type for multiple CSJ part outputs
    class multi_csj_mime : public fostlib::mime {
    public:
        using csj_generator = std::unique_ptr<csj_mime>;

        multi_csj_mime(const fostlib::string &accept, std::vector<csj_generator> g);

        class enumerator : public fostlib::mime::iterator_implementation {
            friend class multi_csj_mime;
            using get_iter = std::vector<csj_generator>::const_iterator;
            get_iter pos, end;
            std::unique_ptr<fostlib::mime::iterator_implementation>  opos;
            enumerator(get_iter b, get_iter e)
            : pos(b), end(e), opos((*b)->iterator()) {
            }
        public:
            fostlib::const_memory_block operator () ();
        };
        std::unique_ptr<iterator_implementation> iterator() const;

        bool boundary_is_ok(const fostlib::string &) const {
            throw fostlib::exceptions::not_implemented(__func__);
        }
        std::ostream &print_on(std::ostream &) const {
            throw fostlib::exceptions::not_implemented(__func__);
        }

    private:
        std::vector<csj_generator> parts;
    };


}
