/*
 * Copyright (c) 2021 Broadcom.
 * The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
 *
 * This program and the accompanying materials are made
 * available under the terms of the Eclipse Public License 2.0
 * which is available at https://www.eclipse.org/legal/epl-2.0/
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 * Contributors:
 *   Broadcom, Inc. - initial API and implementation
 */

#include "proc_grps.h"

#include "assembler_options.h"
#include "instruction_set_version.h"
#include "nlohmann/json.hpp"

namespace hlasm_plugin::parser_library::config {

void to_json(nlohmann::json& j, const library& p)
{
    j = nlohmann::json { { "path", p.path }, { "optional", p.optional } };
    if (auto m = nlohmann::json(p.macro_extensions); !m.empty())
        j["macro_extensions"] = std::move(m);
}
void from_json(const nlohmann::json& j, library& p)
{
    if (j.is_string())
        j.get_to(p.path);
    else if (j.is_object())
    {
        j.at("path").get_to(p.path);
        if (auto it = j.find("optional"); it != j.end())
            p.optional = it->get_to(p.optional);
        if (auto it = j.find("macro_extensions"); it != j.end())
            it->get_to(p.macro_extensions);
    }
    else
        throw nlohmann::json::other_error::create(501, "Unexpected JSON type.", j);
}

void to_json(nlohmann::json& j, const db2_preprocessor& v)
{
    static const db2_preprocessor default_config;
    if (v == default_config)
    {
        j = "DB2";
        return;
    }
    j = nlohmann::json {
        { "name", "DB2" },
        {
            "options",
            {
                { "conditional", v.conditional },
                { "version", v.version },
            },
        },
    };
}
void from_json(const nlohmann::json& j, db2_preprocessor& v)
{
    v = db2_preprocessor {};
    if (!j.is_object())
        return;
    if (auto it = j.find("options"); it != j.end())
    {
        if (!it->is_object())
            throw nlohmann::json::other_error::create(501, "Object with DB2 options expected.", j);
        if (auto ver = it->find("version"); ver != it->end())
        {
            if (!ver->is_string())
                throw nlohmann::json::other_error::create(501, "Version string expected.", j);
            v.version = ver->get<std::string>();
        }
        if (auto cond = it->find("conditional"); cond != it->end())
        {
            if (!cond->is_boolean())
                throw nlohmann::json::other_error::create(501, "Boolean expected.", j);
            v.conditional = cond->get<bool>();
        }
    }
}

void to_json(nlohmann::json& j, const cics_preprocessor& v)
{
    static const cics_preprocessor default_config;
    if (v == default_config)
    {
        j = "CICS";
        return;
    }

    j = nlohmann::json {
        { "name", "CICS" },
        {
            "options",
            nlohmann::json::array({
                v.prolog ? "PROLOG" : "NOPROLOG",
                v.epilog ? "EPILOG" : "NOEPILOG",
                v.leasm ? "LEASM" : "NOLEASM",
            }),
        },
    };
}

namespace {
const std::map<std::string_view, std::pair<bool(cics_preprocessor::*), bool>, std::less<>> cics_preprocessor_options = {
    { "PROLOG", { &cics_preprocessor::prolog, true } },
    { "NOPROLOG", { &cics_preprocessor::prolog, false } },
    { "EPILOG", { &cics_preprocessor::epilog, true } },
    { "NOEPILOG", { &cics_preprocessor::epilog, false } },
    { "LEASM", { &cics_preprocessor::leasm, true } },
    { "NOLEASM", { &cics_preprocessor::leasm, false } },
};
}

void from_json(const nlohmann::json& j, cics_preprocessor& v)
{
    v = cics_preprocessor {};
    if (!j.is_object())
        return;
    if (auto it = j.find("options"); it != j.end())
    {
        if (!it->is_array())
            throw nlohmann::json::other_error::create(501, "Array of CICS options expected.", j);
        for (const auto& e : *it)
        {
            if (!e.is_string())
                throw nlohmann::json::other_error::create(501, "CICS option expected.", j);
            if (auto cpo = cics_preprocessor_options.find(e.get<std::string_view>());
                cpo != cics_preprocessor_options.end())
            {
                const auto [member, value] = cpo->second;
                v.*member = value;
            }
        }
    }
}

namespace {
struct preprocessor_visitor
{
    nlohmann::json& j;

    void operator()(const db2_preprocessor& p) const { j = p; }
    void operator()(const cics_preprocessor& p) const { j = p; }
};
} // namespace

void to_json(nlohmann::json& j, const processor_group& p)
{
    j = nlohmann::json { { "name", p.name }, { "libs", p.libs } };
    if (auto opts = nlohmann::json(p.asm_options); !opts.empty())
        j["asm_options"] = std::move(opts);

    if (p.preprocessors.empty())
    {
        // nothing to do
    }
    else if (p.preprocessors.size() == 1)
    {
        std::visit(preprocessor_visitor { j["preprocessor"] }, p.preprocessors.front().options);
    }
    else
    {
        auto& pp_array = j["preprocessor"] = nlohmann::json::array_t();
        for (const auto& pp : p.preprocessors)
            std::visit(preprocessor_visitor { pp_array.emplace_back() }, pp.options);
    }
}

void from_json(const nlohmann::json& j, processor_group& p)
{
    j.at("name").get_to(p.name);
    j.at("libs").get_to(p.libs);
    if (auto it = j.find("asm_options"); it != j.end())
        it->get_to(p.asm_options);

    if (auto it = j.find("preprocessor"); it != j.end())
    {
        const auto add_single_pp = [&p](nlohmann::json::const_iterator it) {
            std::string p_name;
            if (it->is_string())
                p_name = it->get<std::string>();
            else if (it->is_object())
                it->at("name").get_to(p_name);
            else
                throw nlohmann::json::other_error::create(501, "Unable to identify requested preprocessor.", *it);

            std::transform(
                p_name.begin(), p_name.end(), p_name.begin(), [](unsigned char c) { return (char)toupper(c); });
            if (p_name == "DB2")
                it->get_to(std::get<db2_preprocessor>(
                    p.preprocessors.emplace_back(preprocessor_options { db2_preprocessor() }).options));
            else if (p_name == "CICS")
                it->get_to(std::get<cics_preprocessor>(
                    p.preprocessors.emplace_back(preprocessor_options { cics_preprocessor() }).options));
            else
                throw nlohmann::json::other_error::create(501, "Unable to identify requested preprocessor.", *it);
        };
        if (it->is_array())
        {
            for (auto nested_it = it->begin(); nested_it != it->end(); ++nested_it)
                add_single_pp(nested_it);
        }
        else
            add_single_pp(it);
    }
}

void to_json(nlohmann::json& j, const proc_grps& p)
{
    j = nlohmann::json { { "pgroups", p.pgroups } };
    if (auto m = nlohmann::json(p.macro_extensions); !m.empty())
        j["macro_extensions"] = std::move(m);
}
void from_json(const nlohmann::json& j, proc_grps& p)
{
    j.at("pgroups").get_to(p.pgroups);
    if (auto it = j.find("macro_extensions"); it != j.end())
        it->get_to(p.macro_extensions);
}

namespace {
struct preprocessor_validator
{
    template<typename T>
    bool operator()(const T& t) const noexcept
    {
        return t.valid();
    }
};

struct preprocessor_type_visitor
{
    std::string_view operator()(const db2_preprocessor&) const noexcept { return "DB2"; }
    std::string_view operator()(const cics_preprocessor&) const noexcept { return "CICS"; }
};
} // namespace

bool preprocessor_options::valid() const noexcept { return std::visit(preprocessor_validator {}, options); }

std::string_view preprocessor_options::type() const noexcept
{
    return std::visit(preprocessor_type_visitor(), options);
}

} // namespace hlasm_plugin::parser_library::config
