/*
    Copyright 2016, Felspar Co Ltd. http://support.felspar.com/
    Distributed under the Boost Software License, Version 1.0.
    See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt
*/


#include <fostgres/fostgres.hpp>
#include <fostgres/matcher.hpp>
#include <fostgres/response.hpp>
#include <fostgres/sql.hpp>

#include <fost/csj.parser.hpp>
#include <fost/log>
#include <fost/parse/json.hpp>
#include <fost/push_back>


namespace {


    const fostgres::responder c_csj("csj", fostgres::response_csj);


    struct csj_mime : public fostlib::mime {
        mutable bool done = false;
        mutable std::vector<fostlib::string> columns;
        mutable fostlib::pg::recordset rs;

        struct csj_iterator : public fostlib::mime::iterator_implementation {
            fostlib::pg::recordset rs;
            fostlib::pg::recordset::const_iterator iter, end;
            std::string current;
            bool sent_first = false;

            csj_iterator(std::vector<fostlib::string> &&columns, fostlib::pg::recordset &&r)
            : rs(std::move(r)), iter(rs.begin()), end(rs.end()) {
                current.reserve(64 * 1024);
                fostlib::json::unparse(current, columns[0]);
                for ( std::size_t index{1}; index < columns.size(); ++index ) {
                    current += ',';
                    fostlib::json::unparse(current, columns[index]);
                }
                current += '\n';
                if ( iter != end ) line();
            }

            void line() {
                while ( iter != end && current.length() < 48 * 1024 ) {
                    auto record = *iter;
                    fostlib::json::unparse(current, record[0], false);
                    for ( std::size_t index{1}; index < record.size(); ++index ) {
                        current += ',';
                        fostlib::json::unparse(current, record[index], false);
                    }
                    current += '\n';
                    ++iter;
                }
            }

            fostlib::const_memory_block operator () () {
                if ( !sent_first ) {
                    sent_first = true;
                    return fostlib::const_memory_block(
                        current.c_str(), current.c_str() + current.length());
                } else if ( iter == end ) {
                    return fostlib::const_memory_block();
                } else {
                    current.clear();
                    line();
                    return fostlib::const_memory_block(
                        current.c_str(), current.c_str() + current.length());
                }
            }
        };

        csj_mime(std::vector<fostlib::string> &&cols, fostlib::pg::recordset &&rs)
        : mime(fostlib::mime::mime_headers(), "text/plain"),
            columns(std::move(cols)), rs(std::move(rs))
        {
        }

        std::unique_ptr<iterator_implementation> iterator() const {
            if ( done )
                throw fostlib::exceptions::not_implemented(__FUNCTION__);
            done = true;
            return std::unique_ptr<iterator_implementation>(
                new csj_iterator(std::move(columns), std::move(rs)));
        }

        bool boundary_is_ok(const fostlib::string &) const {
            throw fostlib::exceptions::not_implemented(__FUNCTION__);
        }
        std::ostream &print_on(std::ostream &) const {
            throw fostlib::exceptions::not_implemented(__FUNCTION__);
        }
    };


    std::pair<boost::shared_ptr<fostlib::mime>, int>  get(
        const fostlib::json &config, const fostgres::match &m,
        fostlib::http::server::request &req
    ) {
        auto sql = m.configuration["GET"];
        if ( sql.isobject() ) {
            std::vector<fostlib::json> arguments;
            for ( const auto &arg : sql["arguments"] ) {
                try {
                    arguments.push_back(fostgres::datum(
                        arg, m.arguments, fostlib::json(), req).value(fostlib::json()));
                } catch ( fostlib::exceptions::exception &e ) {
                    insert(e.data(), "datum", arg);
                    throw;
                }
            }
            fostlib::pg::connection cnx(fostgres::connection(config, req));
            auto data = fostgres::sql(cnx, fostlib::coerce<fostlib::string>(sql["command"]), arguments);
            return std::make_pair(
                boost::shared_ptr<fostlib::mime>(
                    new csj_mime(std::move(data.first), std::move(data.second))),
                200);
        } else {
            auto data = m.arguments.size()
                ? fostgres::sql(config, req,
                    fostlib::coerce<fostlib::string>(m.configuration["GET"]),
                    m.arguments)
                : fostgres::sql(config, req,
                    fostlib::coerce<fostlib::string>(m.configuration["GET"]));
            return std::make_pair(
                boost::shared_ptr<fostlib::mime>(
                    new csj_mime(std::move(data.first), std::move(data.second))),
                200);
        }
    }


    std::pair<boost::shared_ptr<fostlib::mime>, int>  patch(
        const fostlib::json &config, const fostgres::match &m,
        fostlib::http::server::request &req
    ) {
        auto logger = fostlib::log::debug(fostgres::c_fostgres);
        logger("", "CSJ PATCH");
        fostlib::json work_done{fostlib::json::object_t()};
        std::size_t records{};

        // We're going to need these items later
        fostlib::pg::connection cnx{fostgres::connection(config, req)};
        fostlib::string relation = fostlib::coerce<fostlib::string>(m.configuration["PATCH"]["table"]);
        fostlib::json col_config = m.configuration["PATCH"]["columns"];
        std::vector<std::pair<fostlib::string, fostlib::json>> col_defs;
        for ( auto col_def = col_config.begin(); col_def != col_config.end(); ++col_def ) {
            col_defs.push_back(std::make_pair(
                fostlib::coerce<fostlib::string>(col_def.key()), *col_def));
        }

        // Interpret body as UTF8 and split into lines. Ensure it's not empty
        fostlib::csj::parser data(fostlib::utf::u8_view(req.data()->data()));
        logger("header", data.header());

        // Parse each line and send it to the database
        for ( auto line(data.begin()), e(data.end()); line != e; ++line ) {
            fostlib::json row = line.as_json();
            fostlib::json keys, values;
            for ( auto &col_def : col_defs ) {
                auto data = fostgres::datum(col_def.first, col_def.second, m.arguments, row, req);
                if ( col_def.second["key"].get(false) ) {
                    // Key column
                    if ( not data.isnull() ) {
                        fostlib::insert(keys, col_def.first, data.value());
                    } else {
                        throw fostlib::exceptions::not_implemented(__FUNCTION__,
                            "Key column doesn't have a value", col_def.first);
                    }
                } else if ( not data.isnull() ) {
                    // Value column
                    fostlib::insert(values, col_def.first, data.value());
                }
            }
            cnx.upsert(relation.c_str(), keys, values);
            ++records;
        }
        cnx.commit();
        fostlib::insert(work_done, "records", records);
        boost::shared_ptr<fostlib::mime> response(
                new fostlib::text_body(fostlib::json::unparse(work_done, true),
                    fostlib::mime::mime_headers(), L"application/json"));
        return std::make_pair(response, 200);
    }


    std::pair<boost::shared_ptr<fostlib::mime>, int>  put(
        const fostlib::json &config, const fostgres::match &m,
        fostlib::http::server::request &req
    ) {
        auto logger = fostlib::log::debug(fostgres::c_fostgres);
        logger("", "CSJ PUT");

        fostlib::pg::connection cnx{fostgres::connection(config, req)};
        fostlib::json work_done{fostlib::json::object_t()};

        // Work out the table and columns we're dealing with
        fostlib::string relation = fostlib::coerce<fostlib::string>(m.configuration["PUT"]["table"]);
        fostlib::json col_config = m.configuration["PUT"]["columns"];
        std::vector<std::pair<fostlib::string, fostlib::json>> col_defs, key_defs;
        for ( auto col_def = col_config.begin(); col_def != col_config.end(); ++col_def ) {
            col_defs.push_back(std::make_pair(
                fostlib::coerce<fostlib::string>(col_def.key()), *col_def));
            if ( (*col_def)["key"].get(false) ) {
                key_defs.push_back(std::make_pair(
                    fostlib::coerce<fostlib::string>(col_def.key()), *col_def));
            }
        }

        // Create a SELECT statement to collect all the associated keys
        // in the database. We need to SELECT across the keys not in
        // the body data and store the keys that are in the body data
        fostlib::json where;
        std::vector<std::vector<fostlib::json>> dbkeys;
        {
            for ( const auto &jkey : m.configuration["PUT"]["where"] ) {
                auto key = fostlib::coerce<fostlib::string>(jkey);
                auto data = fostgres::datum(key, m.configuration["PUT"]["columns"][key],
                    m.arguments, fostlib::json(), req);
                fostlib::insert(where, key, data);
            }
            auto rs = cnx.select(relation.c_str(), where, m.configuration["PUT"]["order"]);
            for ( const auto &row : rs ) {
                std::vector<fostlib::json> keys;
                for ( std::size_t index{}; index != row.size(); ++index ) {
                    keys.push_back(row[index]);
                }
                dbkeys.push_back(keys);
            }
            fostlib::insert(work_done, "selected", dbkeys.size());
        }

        // Process the incoming data and put it into the database. Also
        // record the keys seen

        // Look through the initial keys to find any that weren't in the
        // incoming data so the rows can be deleted

        throw fostlib::exceptions::not_implemented(__func__);
    }


    std::pair<boost::shared_ptr<fostlib::mime>, int>  del(
        const fostlib::json &config, const fostgres::match &m,
        fostlib::http::server::request &req
    ) {
        fostlib::pg::connection cnx{fostgres::connection(config, req)};
        auto sql = fostlib::coerce<fostlib::string>(m.configuration["DELETE"]);
        auto sp = cnx.procedure(fostlib::coerce<fostlib::utf8_string>(sql));
        sp.exec(m.arguments);
        cnx.commit();
        boost::shared_ptr<fostlib::mime> response(new fostlib::text_body(L""));
        return std::make_pair(response, 200);
    }


}


std::pair<boost::shared_ptr<fostlib::mime>, int>  fostgres::response_csj(
    const fostlib::json &config, const fostgres::match &m,
    fostlib::http::server::request &req
) {
    if ( req.method() == "GET" ) {
        return get(config, m, req);
    } else if ( req.method() == "PATCH" ) {
        return patch(config, m, req);
    } else if ( req.method() == "PUT" ) {
        return put(config, m, req);
    } else if ( req.method() == "DELETE" ) {
        return del(config, m, req);
    } else {
        throw fostlib::exceptions::not_implemented(__FUNCTION__,
            "Must use GET, HEAD, DELETE, PUT or PATCH");
    }
}

