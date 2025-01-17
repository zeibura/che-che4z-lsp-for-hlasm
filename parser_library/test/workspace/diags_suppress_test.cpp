/*
 * Copyright (c) 2019 Broadcom.
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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "gtest/gtest.h"

#include "../message_consumer_mock.h"
#include "../workspace/empty_configs.h"
#include "empty_configs.h"
#include "lib_config.h"
#include "nlohmann/json.hpp"
#include "utils/resource_location.h"
#include "workspaces/file_impl.h"
#include "workspaces/file_manager_impl.h"
#include "workspaces/workspace.h"

using namespace nlohmann;
using namespace hlasm_plugin::parser_library;
using namespace hlasm_plugin::parser_library::workspaces;
using namespace hlasm_plugin::utils::resource;

std::string one_proc_grps = R"(
{
	"pgroups": [
        { "name": "P1", "libs": [] }
	]
}
)";

const auto file_loc = resource_location("a_file");

TEST(diags_suppress, no_suppress)
{
    file_manager_impl fm;
    fm.did_open_file(pgm_conf_name, 0, empty_pgm_conf);
    fm.did_open_file(proc_grps_name, 0, one_proc_grps);

    fm.did_open_file(file_loc, 0, R"(
    LR 1,
    LR 1,
    LR 1,
    LR 1,
    LR 1,
    LR 1,
)");

    lib_config config;
    workspace::shared_json global_settings = make_empty_shared_json();

    workspace ws(fm, config, global_settings);
    ws.open();
    ws.did_open_file(file_loc);

    auto pfile = fm.find(file_loc);
    ASSERT_TRUE(pfile);

    pfile->collect_diags();
    EXPECT_EQ(pfile->diags().size(), 6U);
}

TEST(diags_suppress, do_suppress)
{
    auto config = lib_config::load_from_json(R"({"diagnosticsSuppressLimit":5})"_json);
    workspace::shared_json global_settings = make_empty_shared_json();

    file_manager_impl fm;
    fm.did_open_file(pgm_conf_name, 0, empty_pgm_conf);
    fm.did_open_file(proc_grps_name, 0, one_proc_grps);

    fm.did_open_file(file_loc, 0, R"(
    LR 1,
    LR 1,
    LR 1,
    LR 1,
    LR 1,
    LR 1,
)");

    message_consumer_mock msg_consumer;

    workspace ws(fm, config, global_settings);
    ws.set_message_consumer(&msg_consumer);
    ws.open();
    ws.did_open_file(file_loc);

    auto pfile = fm.find(file_loc);
    ASSERT_TRUE(pfile);

    pfile->collect_diags();
    EXPECT_EQ(pfile->diags().size(), 0U);

    ASSERT_EQ(msg_consumer.messages.size(), 1U);
    EXPECT_EQ(msg_consumer.messages[0].first,
        "Diagnostics suppressed from " + file_loc.to_presentable() + ", because there is no configuration.");
    EXPECT_EQ(msg_consumer.messages[0].second, message_type::MT_INFO);
}

TEST(diags_suppress, pgm_supress_limit_changed)
{
    file_manager_impl fm;
    fm.did_open_file(pgm_conf_name, 0, empty_pgm_conf);
    fm.did_open_file(proc_grps_name, 0, one_proc_grps);

    fm.did_open_file(file_loc, 0, R"(
    LR 1,
    LR 1,
    LR 1,
    LR 1,
    LR 1,
    LR 1,
)");

    lib_config config;
    workspace::shared_json global_settings = make_empty_shared_json();

    workspace ws(fm, config, global_settings);
    ws.open();
    ws.did_open_file(file_loc);

    auto pfile = fm.find(file_loc);
    ASSERT_TRUE(pfile);

    pfile->collect_diags();
    EXPECT_EQ(pfile->diags().size(), 6U);
    pfile->diags().clear();

    std::string new_limit_str = R"("diagnosticsSuppressLimit":5,)";
    document_change ch(range({ 0, 1 }, { 0, 1 }), new_limit_str.c_str(), new_limit_str.size());

    fm.did_change_file(pgm_conf_name, 1, &ch, 1);
    ws.did_change_file(pgm_conf_name, &ch, 1);

    ws.did_change_file(file_loc, &ch, 1);

    pfile = fm.find(file_loc);
    ASSERT_TRUE(pfile);
    pfile->collect_diags();
    EXPECT_EQ(pfile->diags().size(), 0U);
}

TEST(diags_suppress, cancel_token)
{
    file_manager_impl fm;
    fm.did_open_file(pgm_conf_name, 0, empty_pgm_conf);
    fm.did_open_file(proc_grps_name, 0, one_proc_grps);

    fm.did_open_file(file_loc, 0, R"(
    LR 1,
    LR 1,
    LR 1,
    LR 1,
    LR 1,
    LR 1,
)");

    std::atomic<bool> cancel = true;
    auto config = lib_config::load_from_json(R"({"diagnosticsSuppressLimit":5})"_json);
    workspace::shared_json global_settings = make_empty_shared_json();

    workspace ws(fm, config, global_settings, &cancel);
    ws.open();
    ws.did_open_file(file_loc);

    auto pfile = fm.find(file_loc);
    ASSERT_TRUE(pfile);

    pfile->collect_diags();
    EXPECT_EQ(pfile->diags().size(), 0U);
}
