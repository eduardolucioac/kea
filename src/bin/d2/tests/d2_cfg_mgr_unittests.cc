// Copyright (C) 2013-2017 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <config.h>

#include <config/module_spec.h>
#include <d2/d2_config.h>
#include <d2/d2_cfg_mgr.h>
#include <d2/d2_simple_parser.h>
#include <d2/parser_context.h>
#include <d2/tests/parser_unittest.h>
#include <dhcpsrv/testutils/config_result_check.h>
#include <process/testutils/d_test_stubs.h>
#include <test_data_files_config.h>
#include <util/encode/base64.h>

#include <boost/foreach.hpp>
#include <boost/scoped_ptr.hpp>
#include <gtest/gtest.h>

using namespace std;
using namespace isc;
using namespace isc::d2;
using namespace isc::process;

namespace {

/// @brief Function to create full path to the spec file
///
/// The full path is dependent upon the value of D2_SRC_DIR which
/// whose value is generated from test_data_files_config.h.in
///
/// @param name file name to which the path should be prepended
std::string specfile(const std::string& name) {
    return (std::string(D2_SRC_DIR) + "/" + name);
}

/// @brief Function to create full path to test data file
///
/// The full path is dependent upon the value of D2_TEST_DATA_DIR which
/// whose value is generated from test_data_files_config.h.in
///
/// @param name file name to which the path should be prepended
std::string testDataFile(const std::string& name) {
    return (std::string(D2_TEST_DATA_DIR) + "/" + name);
}

/// @brief Test fixture class for testing D2CfgMgr class.
/// It maintains an member instance of D2CfgMgr and provides methods for
/// converting JSON strings to configuration element sets, checking parse
/// results, and accessing the configuration context.
class D2CfgMgrTest : public ConfigParseTest {
public:

    /// @brief Constructor
    D2CfgMgrTest():cfg_mgr_(new D2CfgMgr()), d2_params_() {
    }

    /// @brief Destructor
    ~D2CfgMgrTest() {
    }

    /// @brief Configuration manager instance.
    D2CfgMgrPtr cfg_mgr_;

    /// @brief Build JSON configuration string for a D2Params element
    ///
    /// Constructs a JSON string for "params" element using replaceable
    /// parameters.
    ///
    /// @param ip_address string to insert as ip_address value
    /// @param port integer to insert as port value
    /// @param dns_server_timeout integer to insert as dns_server_timeout value
    /// @param ncr_protocol string to insert as ncr_protocol value
    /// @param ncr_format string to insert as ncr_format value
    ///
    /// @return std::string containing the JSON configuration text
    std::string makeParamsConfigString(const std::string& ip_address,
                                       const int port,
                                       const int dns_server_timeout,
                                       const std::string& ncr_protocol,
                                       const std::string& ncr_format) {
        std::ostringstream config;
        config <<
            "{"
            " \"ip-address\": \"" << ip_address << "\" , "
            " \"port\": " << port << " , "
            " \"dns-server-timeout\": " << dns_server_timeout << " , "
            " \"ncr-protocol\": \"" << ncr_protocol << "\" , "
            " \"ncr-format\": \"" << ncr_format << "\", "
            "\"tsig-keys\": [], "
            "\"forward-ddns\" : {}, "
            "\"reverse-ddns\" : {} "
            "}";

        return (config.str());
    }

    /// @brief Enumeration to select between expected configuration outcomes
    enum RunConfigMode {
        NO_ERROR,
        SYNTAX_ERROR,
        LOGIC_ERROR
    };

    /// @brief Parses a configuration string and tests against a given outcome
    ///
    /// Convenience method which accepts JSON text and an expected pass or fail
    /// outcome.  It uses the D2ParserContext to parse the text under the
    /// PARSE_SUB_DHCPDDNS context, then adds the D2 defaults to the resultant
    /// element tree. Assuming that's successful the element tree is passed
    /// to D2CfgMgr::parseConfig() method.
    ///
    /// @param config_str the JSON configuration text to parse
    /// @param error_type  indicates the type error expected, NONE, SYNTAX,
    /// or LOGIC. SYNTAX errors are emitted by JSON parser, logic errors
    /// are emitted by element parser(s).
    /// @param exp_error exact text of the error message expected
    /// defaults to SHOULD_PASS.
    ///
    /// @return AssertionSuccess if test passes, AssertionFailure otherwise
    ::testing::AssertionResult runConfigOrFail(const std::string& json,
                                               const RunConfigMode mode,
                                               const std::string& exp_error) {

        try {
            // Invoke the JSON parser, casting the returned element tree
            // into mutable form.
            D2ParserContext parser_context;
            data::ElementPtr elem =
                boost::const_pointer_cast<Element>
                (parser_context.parseString(json, D2ParserContext::
                                                  PARSER_SUB_DHCPDDNS));

            // If parsing succeeded when we expected a syntax error, then fail.
            if (mode == SYNTAX_ERROR) {
               return ::testing::AssertionFailure()
                             << "Unexpected  JSON parsing success"
                             << "\njson: [" << json << " ]";
            }

            // JSON parsed ok, so add the defaults to the element tree it produced.
            D2SimpleParser::setAllDefaults(elem);
            config_set_ = elem;
        } catch (const std::exception& ex) {
            // JSON Parsing failed
            if (exp_error.empty()) {
                // We did not expect an error, so fail.
                return ::testing::AssertionFailure()
                          << "Unexpected syntax error:" << ex.what()
                          << "\njson: [" << json << " ]";
            }

            if (ex.what() != exp_error) {
                // Expected an error not the one we got, so fail
                return ::testing::AssertionFailure()
                          << "Wrong syntax error detected, expected: "
                          << exp_error << ", got: " << ex.what()
                          << "\njson: [" << json << " ]";
            }

            // We go the syntax error we expected, so return success
            return ::testing::AssertionSuccess();
        }

        // The JSON parsed ok and we've added the defaults, pass the config
        // into the Element parser and check for the expected outcome.
        data::ConstElementPtr answer = cfg_mgr_->parseConfig(config_set_);

        // Extract the result and error text from the answer.
        int rcode = 0;
        isc::data::ConstElementPtr comment;
        comment = isc::config::parseAnswer(rcode, answer);

        if (rcode != 0) {
            // Element Parsing failed.
            if (exp_error.empty()) {
                // We didn't expect it to, fail the test.
                return ::testing::AssertionFailure()
                              << "Unexpected logic error: " << *comment
                              << "\njson: [" << json << " ]";
            }

            if (comment->stringValue() != exp_error) {
                // We 't expect a different error, fail the test.
                return ::testing::AssertionFailure()
                              << "Wrong logic error detected, expected: "
                              << exp_error << ", got: " << *comment
                              << "\njson: [" << json << " ]";
            }
        } else {
            // Element parsing succeeded.
            if (!exp_error.empty()) {
                // It was supposed to fail, so fail the test.
                return ::testing::AssertionFailure()
                              << "Unexpected logic success, expected error:"
                              << exp_error
                              << "\njson: [" << json << " ]";
            }
        }

        // Verify that the D2 context can be retrieved and is not null.
        D2CfgContextPtr context;
        context = cfg_mgr_->getD2CfgContext();
        if (!context) {
            return ::testing::AssertionFailure() << "D2CfgContext is null";
        }

        // Verify that the global scalar container has been created.
        d2_params_ = context->getD2Params();
        if (!d2_params_) {
            return ::testing::AssertionFailure() << "D2Params is null";
        }

        return ::testing::AssertionSuccess();
    }


    /// @brief Pointer the D2Params most recently parsed.
    D2ParamsPtr d2_params_;
};

/// @brief Convenience macros for invoking runOrConfig()
#define RUN_CONFIG_OK(a) (runConfigOrFail(a, NO_ERROR, ""))
#define SYNTAX_ERROR(a,b) ASSERT_TRUE(runConfigOrFail(a,SYNTAX_ERROR,b))
#define LOGIC_ERROR(a,b) ASSERT_TRUE(runConfigOrFail(a,LOGIC_ERROR,b))

/// @brief Tests that the spec file is valid.
/// Verifies that the DHCP-DDNS configuration specification file
///  is valid.
TEST(D2SpecTest, basicSpec) {
    ASSERT_NO_THROW(isc::config::
                    moduleSpecFromFile(specfile("dhcp-ddns.spec")));
}

/// @brief Tests a basic valid configuration for D2Param.
TEST_F(D2CfgMgrTest, validParamsEntry) {
    // Verify that ip_address can be valid v4 address.
    std::string config = makeParamsConfigString ("192.0.0.1", 777, 333,
                                           "UDP", "JSON");
    RUN_CONFIG_OK(config);

    EXPECT_EQ(isc::asiolink::IOAddress("192.0.0.1"),
              d2_params_->getIpAddress());
    EXPECT_EQ(777, d2_params_->getPort());
    EXPECT_EQ(333, d2_params_->getDnsServerTimeout());
    EXPECT_EQ(dhcp_ddns::NCR_UDP, d2_params_->getNcrProtocol());
    EXPECT_EQ(dhcp_ddns::FMT_JSON, d2_params_->getNcrFormat());

    // Verify that ip_address can be valid v6 address.
    config = makeParamsConfigString ("3001::5", 777, 333, "UDP", "JSON");
    RUN_CONFIG_OK(config);

    // Verify that the global scalars have the proper values.
    EXPECT_EQ(isc::asiolink::IOAddress("3001::5"),
              d2_params_->getIpAddress());

    // Verify the configuration summary.
    EXPECT_EQ("listening on 3001::5, port 777, using UDP",
              d2_params_->getConfigSummary());
}

/// @brief Tests default values for D2Params.
/// It verifies that D2Params is populated with default value for optional
/// parameter if not supplied in the configuration.
/// Currently they are all optional.
TEST_F(D2CfgMgrTest, defaultValues) {

    ElementPtr defaults = isc::d2::test::parseJSON("{ }");
    ASSERT_NO_THROW(D2SimpleParser::setAllDefaults(defaults));

    // Check that omitting ip_address gets you its default
    std::string config =
            "{"
            " \"port\": 777 , "
            " \"dns-server-timeout\": 333 , "
            " \"ncr-protocol\": \"UDP\" , "
            " \"ncr-format\": \"JSON\", "
            "\"tsig-keys\": [], "
            "\"forward-ddns\" : {}, "
            "\"reverse-ddns\" : {} "
            "}";

    RUN_CONFIG_OK(config);
    ConstElementPtr deflt;
    ASSERT_NO_THROW(deflt = defaults->get("ip-address"));
    ASSERT_TRUE(deflt);
    EXPECT_EQ(deflt->stringValue(), d2_params_->getIpAddress().toText());

    // Check that omitting port gets you its default
    config =
            "{"
            " \"ip-address\": \"192.0.0.1\" , "
            " \"dns-server-timeout\": 333 , "
            " \"ncr-protocol\": \"UDP\" , "
            " \"ncr-format\": \"JSON\", "
            "\"tsig-keys\": [], "
            "\"forward-ddns\" : {}, "
            "\"reverse-ddns\" : {} "
            "}";

    RUN_CONFIG_OK(config);
    ASSERT_NO_THROW(deflt = defaults->get("port"));
    ASSERT_TRUE(deflt);
    EXPECT_EQ(deflt->intValue(), d2_params_->getPort());

    // Check that omitting timeout gets you its default
    config =
            "{"
            " \"ip-address\": \"192.0.0.1\" , "
            " \"port\": 777 , "
            " \"ncr-protocol\": \"UDP\" , "
            " \"ncr-format\": \"JSON\", "
            "\"tsig-keys\": [], "
            "\"forward-ddns\" : {}, "
            "\"reverse-ddns\" : {} "
            "}";

    RUN_CONFIG_OK(config);
    ASSERT_NO_THROW(deflt = defaults->get("dns-server-timeout"));
    ASSERT_TRUE(deflt);
    EXPECT_EQ(deflt->intValue(), d2_params_->getDnsServerTimeout());

    // Check that omitting protocol gets you its default
    config =
            "{"
            " \"ip-address\": \"192.0.0.1\" , "
            " \"port\": 777 , "
            " \"dns-server-timeout\": 333 , "
            " \"ncr-format\": \"JSON\", "
            "\"tsig-keys\": [], "
            "\"forward-ddns\" : {}, "
            "\"reverse-ddns\" : {} "
            "}";

    RUN_CONFIG_OK(config);
    ASSERT_NO_THROW(deflt = defaults->get("ncr-protocol"));
    ASSERT_TRUE(deflt);
    EXPECT_EQ(dhcp_ddns::stringToNcrProtocol(deflt->stringValue()),
              d2_params_->getNcrProtocol());

    // Check that omitting format gets you its default
    config =
            "{"
            " \"ip-address\": \"192.0.0.1\" , "
            " \"port\": 777 , "
            " \"dns-server-timeout\": 333 , "
            " \"ncr-protocol\": \"UDP\", "
            "\"tsig-keys\": [], "
            "\"forward-ddns\" : {}, "
            "\"reverse-ddns\" : {} "
            "}";

    RUN_CONFIG_OK(config);
    ASSERT_NO_THROW(deflt = defaults->get("ncr-format"));
    ASSERT_TRUE(deflt);
    EXPECT_EQ(dhcp_ddns::stringToNcrFormat(deflt->stringValue()),
              d2_params_->getNcrFormat());
}

/// @brief Tests the unsupported scalar parameters and objects are detected.
TEST_F(D2CfgMgrTest, unsupportedTopLevelItems) {
    // Check that an unsupported top level parameter fails.
    std::string config =
            "{"
            " \"ip-address\": \"127.0.0.1\", "
            " \"port\": 777 , "
            " \"dns-server-timeout\": 333 , "
            " \"ncr-protocol\": \"UDP\" , "
            " \"ncr-format\": \"JSON\", "
            "\"tsig-keys\": [], "
            "\"forward-ddns\" : {}, "
            "\"reverse-ddns\" : {}, "
            "\"bogus-param\" : true "
            "}";

    SYNTAX_ERROR(config, "<string>:1.181-193: got unexpected "
                         "keyword \"bogus-param\" in DhcpDdns map.");

    // Check that unsupported top level objects fails.  For
    // D2 these fail as they are not in the parse order.
    config =
            "{"
            " \"ip-address\": \"127.0.0.1\", "
            " \"port\": 777 , "
            " \"dns-server-timeout\": 333 , "
            " \"ncr-protocol\": \"UDP\" , "
            " \"ncr-format\": \"JSON\", "
            "\"tsig-keys\": [], "
            "\"bogus-object-one\" : {}, "
            "\"forward-ddns\" : {}, "
            "\"reverse-ddns\" : {}, "
            "\"bogus-object-two\" : {} "
            "}";

    SYNTAX_ERROR(config, "<string>:1.139-156: got unexpected"
                         " keyword \"bogus-object-one\" in DhcpDdns map.");
}


/// @brief Tests the enforcement of data validation when parsing D2Params.
/// It verifies that:
/// -# ip_address cannot be "0.0.0.0"
/// -# ip_address cannot be "::"
/// -# port cannot be 0
/// -# dns_server_timeout cannot be 0
/// -# ncr_protocol must be valid
/// -# ncr_format must be valid
TEST_F(D2CfgMgrTest, invalidEntry) {
    // Cannot use IPv4 ANY address
    std::string config = makeParamsConfigString ("0.0.0.0", 777, 333,
                                           "UDP", "JSON");
    LOGIC_ERROR(config, "IP address cannot be \"0.0.0.0\" (<string>:1:17)");

    // Cannot use IPv6 ANY address
    config = makeParamsConfigString ("::", 777, 333, "UDP", "JSON");
    LOGIC_ERROR(config, "IP address cannot be \"::\" (<string>:1:17)");

    // Cannot use port  0
    config = makeParamsConfigString ("127.0.0.1", 0, 333, "UDP", "JSON");
    SYNTAX_ERROR(config, "<string>:1.40: port must be greater than zero but less than 65536");

    // Cannot use dns server timeout of 0
    config = makeParamsConfigString ("127.0.0.1", 777, 0, "UDP", "JSON");
    SYNTAX_ERROR(config, "<string>:1.69: dns-server-timeout"
                         " must be greater than zero");

    // Invalid protocol
    config = makeParamsConfigString ("127.0.0.1", 777, 333, "BOGUS", "JSON");
    SYNTAX_ERROR(config, "<string>:1.92-98: syntax error,"
                         " unexpected constant string, expecting UDP or TCP");

    // Unsupported protocol
    config = makeParamsConfigString ("127.0.0.1", 777, 333, "TCP", "JSON");
    LOGIC_ERROR(config, "ncr-protocol : TCP is not yet supported"
                        "  (<string>:1:92)");

    // Invalid format
    config = makeParamsConfigString ("127.0.0.1", 777, 333, "UDP", "BOGUS");
    SYNTAX_ERROR(config, "<string>:1.115-121: syntax error,"
                         " unexpected constant string, expecting JSON");
}

/// @brief Tests the enforcement of data validation when parsing TSIGKeyInfos.
/// It verifies that:
/// 1. Name cannot be blank.
/// 2. Algorithm cannot be blank.
/// 3. Secret cannot be blank.
TEST_F(TSIGKeyInfoTest, invalidEntry) {
    // Config with a blank name entry.
    std::string config = "{"
                         " \"name\": \"\" , "
                         " \"algorithm\": \"HMAC-MD5\" , "
                         "   \"secret\": \"LSWXnfkKZjdPJI5QxlpnfQ==\" "
                         "}";
    ASSERT_TRUE(fromJSON(config));

    // Verify that build fails on blank name.
    EXPECT_THROW(parser_->build(config_set_), D2CfgError);

    // Config with a blank algorithm entry.
    config = "{"
                         " \"name\": \"d2_key_one\" , "
                         " \"algorithm\": \"\" , "
                         "   \"secret\": \"LSWXnfkKZjdPJI5QxlpnfQ==\" "
                         "}";

    ASSERT_TRUE(fromJSON(config));

    // Verify that build fails on blank algorithm.
    EXPECT_THROW(parser_->build(config_set_), D2CfgError);

    // Config with an invalid algorithm entry.
    config = "{"
                         " \"name\": \"d2_key_one\" , "
                         " \"algorithm\": \"bogus\" , "
                         "   \"secret\": \"LSWXnfkKZjdPJI5QxlpnfQ==\" "
                         "}";

    ASSERT_TRUE(fromJSON(config));

    // Verify that build fails on blank algorithm.
    EXPECT_THROW(parser_->build(config_set_), D2CfgError);

    // Config with a blank secret entry.
    config = "{"
                         " \"name\": \"d2_key_one\" , "
                         " \"algorithm\": \"HMAC-MD5\" , "
                         " \"secret\": \"\" "
                         "}";

    ASSERT_TRUE(fromJSON(config));

    // Verify that build fails blank secret
    EXPECT_THROW(parser_->build(config_set_), D2CfgError);

    // Config with an invalid secret entry.
    config = "{"
                         " \"name\": \"d2_key_one\" , "
                         " \"algorithm\": \"HMAC-MD5\" , "
                         " \"secret\": \"bogus\" "
                         "}";

    ASSERT_TRUE(fromJSON(config));

    // Verify that build fails an invalid secret
    EXPECT_THROW(parser_->build(config_set_), D2CfgError);
}

/// @brief Verifies that TSIGKeyInfo parsing creates a proper TSIGKeyInfo
/// when given a valid combination of entries.
TEST_F(TSIGKeyInfoTest, validEntry) {
    // Valid entries for TSIG key, all items are required.
    std::string config = "{"
                         " \"name\": \"d2_key_one\" , "
                         " \"algorithm\": \"HMAC-MD5\" , "
                         " \"digest-bits\": 120 , "
                         " \"secret\": \"dGhpcyBrZXkgd2lsbCBtYXRjaA==\" "
                         "}";
    ASSERT_TRUE(fromJSON(config));

    // Verify that it builds and commits without throwing.
    //ASSERT_NO_THROW(parser_->build(config_set_));
    (parser_->build(config_set_));
    ASSERT_NO_THROW(parser_->commit());

    // Verify the correct number of keys are present
    int count =  keys_->size();
    EXPECT_EQ(1, count);

    // Find the key and retrieve it.
    TSIGKeyInfoMap::iterator gotit = keys_->find("d2_key_one");
    ASSERT_TRUE(gotit != keys_->end());
    TSIGKeyInfoPtr& key = gotit->second;

    // Verify the key contents.
    EXPECT_TRUE(checkKey(key, "d2_key_one", "HMAC-MD5",
                         "dGhpcyBrZXkgd2lsbCBtYXRjaA==", 120));
}

/// @brief Verifies that attempting to parse an invalid list of TSIGKeyInfo
/// entries is detected.
TEST_F(TSIGKeyInfoTest, invalidTSIGKeyList) {
    // Construct a list of keys with an invalid key entry.
    std::string config = "["

                         " { \"name\": \"key1\" , "
                         "   \"algorithm\": \"HMAC-MD5\" ,"
                         " \"digest-bits\": 120 , "
                         "   \"secret\": \"GWG/Xfbju4O2iXGqkSu4PQ==\" "
                         " },"
                         // this entry has an invalid algorithm
                         " { \"name\": \"key2\" , "
                         "   \"algorithm\": \"\" ,"
                         " \"digest-bits\": 120 , "
                         "   \"secret\": \"GWG/Xfbju4O2iXGqkSu4PQ==\" "
                         " },"
                         " { \"name\": \"key3\" , "
                         "   \"algorithm\": \"HMAC-MD5\" ,"
                         "   \"secret\": \"GWG/Xfbju4O2iXGqkSu4PQ==\" "
                         " }"
                         " ]";

    ASSERT_TRUE(fromJSON(config));

    // Create the list parser.
    isc::dhcp::ParserPtr parser;
    ASSERT_NO_THROW(parser.reset(new TSIGKeyInfoListParser("test", keys_)));

    // Verify that the list builds without errors.
    EXPECT_THROW(parser->build(config_set_), D2CfgError);
}

/// @brief Verifies that attempting to parse an invalid list of TSIGKeyInfo
/// entries is detected.
TEST_F(TSIGKeyInfoTest, duplicateTSIGKey) {
    // Construct a list of keys with an invalid key entry.
    std::string config = "["

                         " { \"name\": \"key1\" , "
                         "   \"algorithm\": \"HMAC-MD5\" ,"
                         " \"digest-bits\": 120 , "
                         "   \"secret\": \"GWG/Xfbju4O2iXGqkSu4PQ==\" "
                         " },"
                         " { \"name\": \"key2\" , "
                         "   \"algorithm\": \"HMAC-MD5\" ,"
                         " \"digest-bits\": 120 , "
                         "   \"secret\": \"GWG/Xfbju4O2iXGqkSu4PQ==\" "
                         " },"
                         " { \"name\": \"key1\" , "
                         "   \"algorithm\": \"HMAC-MD5\" ,"
                         "   \"secret\": \"GWG/Xfbju4O2iXGqkSu4PQ==\" "
                         " }"
                         " ]";

    ASSERT_TRUE(fromJSON(config));

    // Create the list parser.
    isc::dhcp::ParserPtr parser;
    ASSERT_NO_THROW(parser.reset(new TSIGKeyInfoListParser("test", keys_)));

    // Verify that the list builds without errors.
    EXPECT_THROW(parser->build(config_set_), D2CfgError);
}

/// @brief Verifies a valid list of TSIG Keys parses correctly.
/// Also verifies that all of the supported algorithm names work.
TEST_F(TSIGKeyInfoTest, validTSIGKeyList) {
    // Construct a valid list of keys.
    std::string config = "["

                         " { \"name\": \"key1\" , "
                         "   \"algorithm\": \"HMAC-MD5\" ,"
                         " \"digest-bits\": 80 , "
                         "  \"secret\": \"dGhpcyBrZXkgd2lsbCBtYXRjaA==\" "
                         " },"
                         " { \"name\": \"key2\" , "
                         "   \"algorithm\": \"HMAC-SHA1\" ,"
                         " \"digest-bits\": 80 , "
                         "  \"secret\": \"dGhpcyBrZXkgd2lsbCBtYXRjaA==\" "
                         " },"
                         " { \"name\": \"key3\" , "
                         "   \"algorithm\": \"HMAC-SHA256\" ,"
                         " \"digest-bits\": 128 , "
                         "  \"secret\": \"dGhpcyBrZXkgd2lsbCBtYXRjaA==\" "
                         " },"
                         " { \"name\": \"key4\" , "
                         "   \"algorithm\": \"HMAC-SHA224\" ,"
                         " \"digest-bits\": 112 , "
                         "  \"secret\": \"dGhpcyBrZXkgd2lsbCBtYXRjaA==\" "
                         " },"
                         " { \"name\": \"key5\" , "
                         "   \"algorithm\": \"HMAC-SHA384\" ,"
                         " \"digest-bits\": 192 , "
                         "  \"secret\": \"dGhpcyBrZXkgd2lsbCBtYXRjaA==\" "
                         " },"
                         " { \"name\": \"key6\" , "
                         "   \"algorithm\": \"HMAC-SHA512\" ,"
                         " \"digest-bits\": 256 , "
                         "   \"secret\": \"dGhpcyBrZXkgd2lsbCBtYXRjaA==\" "
                         " }"
                         " ]";

    ASSERT_TRUE(fromJSON(config));

    // Verify that the list builds and commits without errors.
    // Create the list parser.
    isc::dhcp::ParserPtr parser;
    ASSERT_NO_THROW(parser.reset(new TSIGKeyInfoListParser("test", keys_)));
    ASSERT_NO_THROW(parser->build(config_set_));
    ASSERT_NO_THROW(parser->commit());

    std::string ref_secret = "dGhpcyBrZXkgd2lsbCBtYXRjaA==";
    // Verify the correct number of keys are present
    int count =  keys_->size();
    ASSERT_EQ(6, count);

    // Find the 1st key and retrieve it.
    TSIGKeyInfoMap::iterator gotit = keys_->find("key1");
    ASSERT_TRUE(gotit != keys_->end());
    TSIGKeyInfoPtr& key = gotit->second;

    // Verify the key contents.
    EXPECT_TRUE(checkKey(key, "key1", TSIGKeyInfo::HMAC_MD5_STR,
                         ref_secret, 80));

    // Find the 2nd key and retrieve it.
    gotit = keys_->find("key2");
    ASSERT_TRUE(gotit != keys_->end());
    key = gotit->second;

    // Verify the key contents.
    EXPECT_TRUE(checkKey(key, "key2", TSIGKeyInfo::HMAC_SHA1_STR,
                         ref_secret, 80));

    // Find the 3rd key and retrieve it.
    gotit = keys_->find("key3");
    ASSERT_TRUE(gotit != keys_->end());
    key = gotit->second;

    // Verify the key contents.
    EXPECT_TRUE(checkKey(key, "key3", TSIGKeyInfo::HMAC_SHA256_STR,
                         ref_secret, 128));

    // Find the 4th key and retrieve it.
    gotit = keys_->find("key4");
    ASSERT_TRUE(gotit != keys_->end());
    key = gotit->second;

    // Verify the key contents.
    EXPECT_TRUE(checkKey(key, "key4", TSIGKeyInfo::HMAC_SHA224_STR,
                         ref_secret, 112));

    // Find the 5th key and retrieve it.
    gotit = keys_->find("key5");
    ASSERT_TRUE(gotit != keys_->end());
    key = gotit->second;

    // Verify the key contents.
    EXPECT_TRUE(checkKey(key, "key5", TSIGKeyInfo::HMAC_SHA384_STR,
                         ref_secret, 192));

    // Find the 6th key and retrieve it.
    gotit = keys_->find("key6");
    ASSERT_TRUE(gotit != keys_->end());
    key = gotit->second;

    // Verify the key contents.
    EXPECT_TRUE(checkKey(key, "key6", TSIGKeyInfo::HMAC_SHA512_STR,
                         ref_secret, 256));
}

/// @brief Tests the enforcement of data validation when parsing DnsServerInfos.
/// It verifies that:
/// 1. Specifying both a hostname and an ip address is not allowed.
/// 2. Specifying both blank a hostname and blank ip address is not allowed.
/// 3. Specifying a negative port number is not allowed.
TEST_F(DnsServerInfoTest, invalidEntry) {
    // Create a config in which both host and ip address are supplied.
    // Verify that build fails.
    std::string config = "{ \"hostname\": \"pegasus.tmark\", "
                         "  \"ip-address\": \"127.0.0.1\" } ";
    ASSERT_TRUE(fromJSON(config));
    EXPECT_THROW(parser_->build(config_set_), D2CfgError);

    // Neither host nor ip address supplied
    // Verify that builds fails.
    config = "{ \"hostname\": \"\", "
             "  \"ip-address\": \"\" } ";
    ASSERT_TRUE(fromJSON(config));
    EXPECT_THROW(parser_->build(config_set_), D2CfgError);

    // Create a config with a negative port number.
    // Verify that build fails.
    config = "{ \"ip-address\": \"192.168.5.6\" ,"
             "  \"port\": -100 }";
    ASSERT_TRUE(fromJSON(config));
    EXPECT_THROW (parser_->build(config_set_), isc::BadValue);
}


/// @brief Verifies that DnsServerInfo parsing creates a proper DnsServerInfo
/// when given a valid combination of entries.
/// It verifies that:
/// 1. A DnsServerInfo entry is correctly made, when given only a hostname.
/// 2. A DnsServerInfo entry is correctly made, when given ip address and port.
/// 3. A DnsServerInfo entry is correctly made, when given only an ip address.
TEST_F(DnsServerInfoTest, validEntry) {
    /// @todo When resolvable hostname is supported you'll need this test.
    /// // Valid entries for dynamic host
    /// std::string config = "{ \"hostname\": \"pegasus.tmark\" }";
    /// ASSERT_TRUE(fromJSON(config));

    /// // Verify that it builds and commits without throwing.
    /// ASSERT_NO_THROW(parser_->build(config_set_));
    /// ASSERT_NO_THROW(parser_->commit());

    /// //Verify the correct number of servers are present
    /// int count =  servers_->size();
    /// EXPECT_EQ(1, count);

    /// Verify the server exists and has the correct values.
    /// DnsServerInfoPtr server = (*servers_)[0];
    /// EXPECT_TRUE(checkServer(server, "pegasus.tmark",
    ///                         DnsServerInfo::EMPTY_IP_STR,
    ///                         DnsServerInfo::STANDARD_DNS_PORT));

    /// // Start over for a new test.
    /// reset();

    // Valid entries for static ip
    std::string config = " { \"ip-address\": \"127.0.0.1\" , "
                         "  \"port\": 100 }";
    ASSERT_TRUE(fromJSON(config));

    // Verify that it builds and commits without throwing.
    ASSERT_NO_THROW(parser_->build(config_set_));
    ASSERT_NO_THROW(parser_->commit());

    // Verify the correct number of servers are present
    int count =  servers_->size();
    EXPECT_EQ(1, count);

    // Verify the server exists and has the correct values.
    DnsServerInfoPtr server = (*servers_)[0];
    EXPECT_TRUE(checkServer(server, "", "127.0.0.1", 100));

    // Start over for a new test.
    reset();

    // Valid entries for static ip, no port
    config = " { \"ip-address\": \"192.168.2.5\" }";
    ASSERT_TRUE(fromJSON(config));

    // Verify that it builds and commits without throwing.
    ASSERT_NO_THROW(parser_->build(config_set_));
    ASSERT_NO_THROW(parser_->commit());

    // Verify the correct number of servers are present
    count =  servers_->size();
    EXPECT_EQ(1, count);

    // Verify the server exists and has the correct values.
    server = (*servers_)[0];
    EXPECT_TRUE(checkServer(server, "", "192.168.2.5",
                            DnsServerInfo::STANDARD_DNS_PORT));
}

/// @brief Verifies that attempting to parse an invalid list of DnsServerInfo
/// entries is detected.
TEST_F(ConfigParseTest, invalidServerList) {
    // Construct a list of servers with an invalid server entry.
    std::string config = "[ { \"ip-address\": \"127.0.0.1\" }, "
                        "{ \"ip-address\": \"\" }, "
                        "{ \"ip-address\": \"127.0.0.2\" } ]";
    ASSERT_TRUE(fromJSON(config));

    // Create the server storage and list parser.
    DnsServerInfoStoragePtr servers(new DnsServerInfoStorage());
    isc::dhcp::ParserPtr parser;
    ASSERT_NO_THROW(parser.reset(new DnsServerInfoListParser("test", servers)));

    // Verify that build fails.
    EXPECT_THROW(parser->build(config_set_), D2CfgError);
}

/// @brief Verifies that a list of DnsServerInfo entries parses correctly given
/// a valid configuration.
TEST_F(ConfigParseTest, validServerList) {
    // Create a valid list of servers.
    std::string config = "[ { \"ip-address\": \"127.0.0.1\" }, "
                        "{ \"ip-address\": \"127.0.0.2\" }, "
                        "{ \"ip-address\": \"127.0.0.3\" } ]";
    ASSERT_TRUE(fromJSON(config));

    // Create the server storage and list parser.
    DnsServerInfoStoragePtr servers(new DnsServerInfoStorage());
    isc::dhcp::ParserPtr parser;
    ASSERT_NO_THROW(parser.reset(new DnsServerInfoListParser("test", servers)));

    // Verify that the list builds and commits without error.
    ASSERT_NO_THROW(parser->build(config_set_));
    ASSERT_NO_THROW(parser->commit());

    // Verify that the server storage contains the correct number of servers.
    int count =  servers->size();
    EXPECT_EQ(3, count);

    // Verify the first server exists and has the correct values.
    DnsServerInfoPtr server = (*servers)[0];
    EXPECT_TRUE(checkServer(server, "", "127.0.0.1",
                            DnsServerInfo::STANDARD_DNS_PORT));

    // Verify the second server exists and has the correct values.
    server = (*servers)[1];
    EXPECT_TRUE(checkServer(server, "", "127.0.0.2",
                            DnsServerInfo::STANDARD_DNS_PORT));

    // Verify the third server exists and has the correct values.
    server = (*servers)[2];
    EXPECT_TRUE(checkServer(server, "", "127.0.0.3",
                            DnsServerInfo::STANDARD_DNS_PORT));
}

/// @brief Tests the enforcement of data validation when parsing DdnsDomains.
/// It verifies that:
/// 1. Domain storage cannot be null when constructing a DdnsDomainParser.
/// 2. The name entry is not optional.
/// 3. The server list man not be empty.
/// 4. That a mal-formed server entry is detected.
/// 5. That an undefined key name is detected.
TEST_F(DdnsDomainTest, invalidDdnsDomainEntry) {
    // Verify that attempting to construct the parser with null storage fails.
    DdnsDomainMapPtr domains;
    ASSERT_THROW(isc::dhcp::ParserPtr(
                 new DdnsDomainParser("test", domains, keys_)), D2CfgError);

    // Create a domain configuration without a name
    std::string config = "{  \"key-name\": \"d2_key.tmark.org\" , "
                         "  \"dns-servers\" : [ "
                         "  {  \"ip-address\": \"127.0.0.1\" , "
                         "    \"port\": 100 },"
                         "  { \"ip-address\": \"127.0.0.2\" , "
                         "    \"port\": 200 },"
                         "  {  \"ip-address\": \"127.0.0.3\" , "
                         "    \"port\": 300 } ] } ";
    ASSERT_TRUE(fromJSON(config));

    // Verify that the domain configuration builds fails.
    EXPECT_THROW(parser_->build(config_set_), D2CfgError);

    // Create a domain configuration with an empty server list.
    config = "{ \"name\": \"tmark.org\" , "
             "  \"key-name\": \"d2_key.tmark.org\" , "
             "  \"dns-servers\" : [ "
             "   ] } ";
    ASSERT_TRUE(fromJSON(config));

    // Verify that the domain configuration build fails.
    EXPECT_THROW(parser_->build(config_set_), D2CfgError);

    // Create a domain configuration with a mal-formed server entry.
    config = "{ \"name\": \"tmark.org\" , "
             "  \"key-name\": \"d2_key.tmark.org\" , "
             "  \"dns-servers\" : [ "
             "  {  \"ip-address\": \"127.0.0.3\" , "
             "    \"port\": -1 } ] } ";
    ASSERT_TRUE(fromJSON(config));

    // Verify that the domain configuration build fails.
    EXPECT_THROW(parser_->build(config_set_), isc::BadValue);

    // Create a domain configuration without an defined key name
    config = "{ \"name\": \"tmark.org\" , "
             "  \"key-name\": \"d2_key.tmark.org\" , "
             "  \"dns-servers\" : [ "
             "  {  \"ip-address\": \"127.0.0.3\" , "
             "    \"port\": 300 } ] } ";
    ASSERT_TRUE(fromJSON(config));

    // Verify that the domain configuration build fails.
    EXPECT_THROW(parser_->build(config_set_), D2CfgError);
}

/// @brief Verifies the basics of parsing DdnsDomains.
/// It verifies that:
/// 1. Valid construction of DdnsDomainParser functions.
/// 2. Given a valid, configuration entry, DdnsDomainParser parses
/// correctly.
/// (It indirectly verifies the operation of DdnsDomainMap).
TEST_F(DdnsDomainTest, ddnsDomainParsing) {
    // Create a valid domain configuration entry containing three valid
    // servers.
    std::string config =
                        "{ \"name\": \"tmark.org\" , "
                        "  \"key-name\": \"d2_key.tmark.org\" , "
                        "  \"dns-servers\" : [ "
                        "  {  \"ip-address\": \"127.0.0.1\" , "
                        "    \"port\": 100 },"
                        "  { \"ip-address\": \"127.0.0.2\" , "
                        "    \"port\": 200 },"
                        "  {  \"ip-address\": \"127.0.0.3\" , "
                        "    \"port\": 300 } ] } ";
    ASSERT_TRUE(fromJSON(config));

    // Add a TSIG key to the test key map, so key validation will pass.
    addKey("d2_key.tmark.org", "HMAC-MD5", "GWG/Xfbju4O2iXGqkSu4PQ==");

    // Verify that the domain configuration builds and commits without error.
    ASSERT_NO_THROW(parser_->build(config_set_));
    ASSERT_NO_THROW(parser_->commit());

    // Verify that the domain storage contains the correct number of domains.
    int count =  domains_->size();
    EXPECT_EQ(1, count);

    // Verify that the expected domain exists and can be retrieved from
    // the storage.
    DdnsDomainMap::iterator gotit = domains_->find("tmark.org");
    ASSERT_TRUE(gotit != domains_->end());
    DdnsDomainPtr& domain = gotit->second;

    // Verify the name and key_name values.
    EXPECT_EQ("tmark.org", domain->getName());
    EXPECT_EQ("d2_key.tmark.org", domain->getKeyName());
    ASSERT_TRUE(domain->getTSIGKeyInfo());
    ASSERT_TRUE(domain->getTSIGKeyInfo()->getTSIGKey());

    // Verify that the server list exists and contains the correct number of
    // servers.
    const DnsServerInfoStoragePtr& servers = domain->getServers();
    EXPECT_TRUE(servers);
    count =  servers->size();
    EXPECT_EQ(3, count);

    // Fetch each server and verify its contents.
    DnsServerInfoPtr server = (*servers)[0];
    EXPECT_TRUE(server);

    EXPECT_TRUE(checkServer(server, "", "127.0.0.1", 100));

    server = (*servers)[1];
    EXPECT_TRUE(server);

    EXPECT_TRUE(checkServer(server, "", "127.0.0.2", 200));

    server = (*servers)[2];
    EXPECT_TRUE(server);

    EXPECT_TRUE(checkServer(server, "", "127.0.0.3", 300));
}

/// @brief Tests the fundamentals of parsing DdnsDomain lists.
/// This test verifies that given a valid domain list configuration
/// it will accurately parse and populate each domain in the list.
TEST_F(DdnsDomainTest, DdnsDomainListParsing) {
    // Create a valid domain list configuration, with two domains
    // that have three servers each.
    std::string config =
                        "[ "
                        "{ \"name\": \"tmark.org\" , "
                        "  \"key-name\": \"d2_key.tmark.org\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.0.1\" , "
                        "    \"port\": 100 },"
                        "  { \"ip-address\": \"127.0.0.2\" , "
                        "    \"port\": 200 },"
                        "  { \"ip-address\": \"127.0.0.3\" , "
                        "    \"port\": 300 } ] } "
                        ", "
                        "{ \"name\": \"billcat.net\" , "
                        "  \"key-name\": \"d2_key.billcat.net\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.0.4\" , "
                        "    \"port\": 400 },"
                        "  { \"ip-address\": \"127.0.0.5\" , "
                        "    \"port\": 500 },"
                        "  { \"ip-address\": \"127.0.0.6\" , "
                        "    \"port\": 600 } ] } "
                        "] ";

    ASSERT_TRUE(fromJSON(config));

    // Add keys to key map so key validation passes.
    addKey("d2_key.tmark.org", "HMAC-MD5", "GWG/Xfbju4O2iXGqkSu4PQ==");
    addKey("d2_key.billcat.net", "HMAC-MD5", "GWG/Xfbju4O2iXGqkSu4PQ==");

    // Create the list parser
    isc::dhcp::ParserPtr list_parser;
    ASSERT_NO_THROW(list_parser.reset(
                    new DdnsDomainListParser("test", domains_, keys_)));

    // Verify that the domain configuration builds and commits without error.
    ASSERT_NO_THROW(list_parser->build(config_set_));
    ASSERT_NO_THROW(list_parser->commit());

    // Verify that the domain storage contains the correct number of domains.
    int count =  domains_->size();
    EXPECT_EQ(2, count);

    // Verify that the first domain exists and can be retrieved.
    DdnsDomainMap::iterator gotit = domains_->find("tmark.org");
    ASSERT_TRUE(gotit != domains_->end());
    DdnsDomainPtr& domain = gotit->second;

    // Verify the name and key_name values of the first domain.
    EXPECT_EQ("tmark.org", domain->getName());
    EXPECT_EQ("d2_key.tmark.org", domain->getKeyName());
    ASSERT_TRUE(domain->getTSIGKeyInfo());
    ASSERT_TRUE(domain->getTSIGKeyInfo()->getTSIGKey());

    // Verify the each of the first domain's servers
    DnsServerInfoStoragePtr servers = domain->getServers();
    EXPECT_TRUE(servers);
    count =  servers->size();
    EXPECT_EQ(3, count);

    DnsServerInfoPtr server = (*servers)[0];
    EXPECT_TRUE(server);
    EXPECT_TRUE(checkServer(server, "", "127.0.0.1", 100));

    server = (*servers)[1];
    EXPECT_TRUE(server);
    EXPECT_TRUE(checkServer(server, "", "127.0.0.2", 200));

    server = (*servers)[2];
    EXPECT_TRUE(server);
    EXPECT_TRUE(checkServer(server, "", "127.0.0.3", 300));

    // Verify second domain
    gotit = domains_->find("billcat.net");
    ASSERT_TRUE(gotit != domains_->end());
    domain = gotit->second;

    // Verify the name and key_name values of the second domain.
    EXPECT_EQ("billcat.net", domain->getName());
    EXPECT_EQ("d2_key.billcat.net", domain->getKeyName());
    ASSERT_TRUE(domain->getTSIGKeyInfo());
    ASSERT_TRUE(domain->getTSIGKeyInfo()->getTSIGKey());

    // Verify the each of second domain's servers
    servers = domain->getServers();
    EXPECT_TRUE(servers);
    count =  servers->size();
    EXPECT_EQ(3, count);

    server = (*servers)[0];
    EXPECT_TRUE(server);
    EXPECT_TRUE(checkServer(server, "", "127.0.0.4", 400));

    server = (*servers)[1];
    EXPECT_TRUE(server);
    EXPECT_TRUE(checkServer(server, "", "127.0.0.5", 500));

    server = (*servers)[2];
    EXPECT_TRUE(server);
    EXPECT_TRUE(checkServer(server, "", "127.0.0.6", 600));
}

/// @brief Tests that a domain list configuration cannot contain duplicates.
TEST_F(DdnsDomainTest, duplicateDomain) {
    // Create a domain list configuration that contains two domains with
    // the same name.
    std::string config =
                        "[ "
                        "{ \"name\": \"tmark.org\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.0.3\" , "
                        "    \"port\": 300 } ] } "
                        ", "
                        "{ \"name\": \"tmark.org\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.0.3\" , "
                        "    \"port\": 300 } ] } "
                        "] ";
    ASSERT_TRUE(fromJSON(config));

    // Create the list parser
    isc::dhcp::ParserPtr list_parser;
    ASSERT_NO_THROW(list_parser.reset(
                    new DdnsDomainListParser("test", domains_, keys_)));

    // Verify that the parse build fails.
    EXPECT_THROW(list_parser->build(config_set_), D2CfgError);
}

/// @brief Tests construction of D2CfgMgr
/// This test verifies that a D2CfgMgr constructs properly.
TEST(D2CfgMgr, construction) {
    boost::scoped_ptr<D2CfgMgr> cfg_mgr;

    // Verify that configuration manager constructions without error.
    ASSERT_NO_THROW(cfg_mgr.reset(new D2CfgMgr()));

    // Verify that the context can be retrieved and is not null.
    D2CfgContextPtr context;
    ASSERT_NO_THROW(context = cfg_mgr->getD2CfgContext());
    EXPECT_TRUE(context);

    // Verify that the forward manager can be retrieved and is not null.
    EXPECT_TRUE(context->getForwardMgr());

    // Verify that the reverse manager can be retrieved and is not null.
    EXPECT_TRUE(context->getReverseMgr());

    // Verify that the manager can be destructed without error.
    EXPECT_NO_THROW(cfg_mgr.reset());
}

/// @brief Tests the parsing of a complete, valid DHCP-DDNS configuration.
/// This tests passes the configuration into an instance of D2CfgMgr just
/// as it would be done by d2_process in response to a configuration update
/// event.
TEST_F(D2CfgMgrTest, fullConfig) {
    // Create a configuration with all of application level parameters, plus
    // both the forward and reverse ddns managers.  Both managers have two
    // domains with three servers per domain.
    std::string config = "{ "
                        "\"ip-address\" : \"192.168.1.33\" , "
                        "\"port\" : 88 , "
                        " \"dns-server-timeout\": 333 , "
                        " \"ncr-protocol\": \"UDP\" , "
                        " \"ncr-format\": \"JSON\", "
                        "\"tsig-keys\": ["
                        "{"
                        "  \"name\": \"d2_key.example.com\" , "
                        "  \"algorithm\": \"hmac-md5\" , "
                        "   \"secret\": \"LSWXnfkKZjdPJI5QxlpnfQ==\" "
                        "},"
                        "{"
                        "  \"name\": \"d2_key.billcat.net\" , "
                        "  \"algorithm\": \"hmac-md5\" , "
                        " \"digest-bits\": 120 , "
                        "   \"secret\": \"LSWXnfkKZjdPJI5QxlpnfQ==\" "
                        "}"
                        "],"
                        "\"forward-ddns\" : {"
                        "\"ddns-domains\": [ "
                        "{ \"name\": \"example.com\" , "
                        "  \"key-name\": \"d2_key.example.com\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.0.1\" } , "
                        "  { \"ip-address\": \"127.0.0.2\" } , "
                        "  { \"ip-address\": \"127.0.0.3\"} "
                        "  ] } "
                        ", "
                        "{ \"name\": \"billcat.net\" , "
                        "  \"key-name\": \"d2_key.billcat.net\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.0.4\" } , "
                        "  { \"ip-address\": \"127.0.0.5\" } , "
                        "  { \"ip-address\": \"127.0.0.6\" } "
                        "  ] } "
                        "] },"
                        "\"reverse-ddns\" : {"
                        "\"ddns-domains\": [ "
                        "{ \"name\": \" 0.168.192.in.addr.arpa.\" , "
                        "  \"key-name\": \"d2_key.example.com\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.1.1\" } , "
                        "  { \"ip-address\": \"127.0.2.1\" } , "
                        "  { \"ip-address\": \"127.0.3.1\" } "
                        "  ] } "
                        ", "
                        "{ \"name\": \" 0.247.106.in.addr.arpa.\" , "
                        "  \"key-name\": \"d2_key.billcat.net\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.4.1\" }, "
                        "  { \"ip-address\": \"127.0.5.1\" } , "
                        "  { \"ip-address\": \"127.0.6.1\" } "
                        "  ] } "
                        "] } }";

    // Should parse without error.
    RUN_CONFIG_OK(config);

    // Verify that the D2 context can be retrieved and is not null.
    D2CfgContextPtr context;
    ASSERT_NO_THROW(context = cfg_mgr_->getD2CfgContext());

    // Verify that the global scalars have the proper values.
    D2ParamsPtr& d2_params = context->getD2Params();
    ASSERT_TRUE(d2_params);

    EXPECT_EQ(isc::asiolink::IOAddress("192.168.1.33"),
              d2_params->getIpAddress());
    EXPECT_EQ(88, d2_params->getPort());
    EXPECT_EQ(333, d2_params->getDnsServerTimeout());
    EXPECT_EQ(dhcp_ddns::NCR_UDP, d2_params->getNcrProtocol());
    EXPECT_EQ(dhcp_ddns::FMT_JSON, d2_params->getNcrFormat());

    // Verify that the forward manager can be retrieved.
    DdnsDomainListMgrPtr mgr = context->getForwardMgr();
    ASSERT_TRUE(mgr);

    // Verify that the forward manager has the correct number of domains.
    DdnsDomainMapPtr domains = mgr->getDomains();
    ASSERT_TRUE(domains);
    int count =  domains->size();
    EXPECT_EQ(2, count);

    // Verify that the server count in each of the forward manager domains.
    // NOTE that since prior tests have validated server parsing, we are are
    // assuming that the servers did in fact parse correctly if the correct
    // number of them are there.
    DdnsDomainMapPair domain_pair;
    BOOST_FOREACH(domain_pair, (*domains)) {
        DdnsDomainPtr domain = domain_pair.second;
        DnsServerInfoStoragePtr servers = domain->getServers();
        count = servers->size();
        EXPECT_TRUE(servers);
        EXPECT_EQ(3, count);
    }

    // Verify that the reverse manager can be retrieved.
    mgr = context->getReverseMgr();
    ASSERT_TRUE(mgr);

    // Verify that the reverse manager has the correct number of domains.
    domains = mgr->getDomains();
    count =  domains->size();
    EXPECT_EQ(2, count);

    // Verify that the server count in each of the reverse manager domains.
    // NOTE that since prior tests have validated server parsing, we are are
    // assuming that the servers did in fact parse correctly if the correct
    // number of them are there.
    BOOST_FOREACH(domain_pair, (*domains)) {
        DdnsDomainPtr domain = domain_pair.second;
        DnsServerInfoStoragePtr servers = domain->getServers();
        count = servers->size();
        EXPECT_TRUE(servers);
        EXPECT_EQ(3, count);
    }

    // Test directional update flags.
    EXPECT_TRUE(cfg_mgr_->forwardUpdatesEnabled());
    EXPECT_TRUE(cfg_mgr_->reverseUpdatesEnabled());

    // Verify that parsing the exact same configuration a second time
    // does not cause a duplicate value errors.
    answer_ = cfg_mgr_->parseConfig(config_set_);
    ASSERT_TRUE(checkAnswer(0));
}

/// @brief Tests the basics of the D2CfgMgr FQDN-domain matching
/// This test uses a valid configuration to exercise the D2CfgMgr
/// forward FQDN-to-domain matching.
/// It verifies that:
/// 1. Given an FQDN which exactly matches a domain's name, that domain is
/// returned as match.
/// 2. Given a FQDN for sub-domain in the list, returns the proper match.
/// 3. Given a FQDN that matches no domain name, returns the wild card domain
/// as a match.
TEST_F(D2CfgMgrTest, forwardMatch) {
    // Create  configuration with one domain, one sub domain, and the wild
    // card.
    std::string config = "{ "
                        "\"ip-address\" : \"192.168.1.33\" , "
                        "\"port\" : 88 , "
                        "\"tsig-keys\": [] ,"
                        "\"forward-ddns\" : {"
                        "\"ddns-domains\": [ "
                        "{ \"name\": \"example.com\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.0.1\" } "
                        "  ] } "
                        ", "
                        "{ \"name\": \"one.example.com\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.0.2\" } "
                        "  ] } "
                        ", "
                        "{ \"name\": \"*\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.0.3\" } "
                        "  ] } "
                        "] }, "
                        "\"reverse-ddns\" : {} "
                        "}";

    // Verify that we can parse the configuration.
    RUN_CONFIG_OK(config);

    // Verify that the D2 context can be retrieved and is not null.
    D2CfgContextPtr context;
    ASSERT_NO_THROW(context = cfg_mgr_->getD2CfgContext());

    // Test directional update flags.
    EXPECT_TRUE(cfg_mgr_->forwardUpdatesEnabled());
    EXPECT_FALSE(cfg_mgr_->reverseUpdatesEnabled());

    DdnsDomainPtr match;
    // Verify that an exact match works.
    EXPECT_TRUE(cfg_mgr_->matchForward("example.com", match));
    EXPECT_EQ("example.com", match->getName());

    // Verify that search is case insensitive.
    EXPECT_TRUE(cfg_mgr_->matchForward("EXAMPLE.COM", match));
    EXPECT_EQ("example.com", match->getName());

    // Verify that an exact match works.
    EXPECT_TRUE(cfg_mgr_->matchForward("one.example.com", match));
    EXPECT_EQ("one.example.com", match->getName());

    // Verify that a FQDN for sub-domain matches.
    EXPECT_TRUE(cfg_mgr_->matchForward("blue.example.com", match));
    EXPECT_EQ("example.com", match->getName());

    // Verify that a FQDN for sub-domain matches.
    EXPECT_TRUE(cfg_mgr_->matchForward("red.one.example.com", match));
    EXPECT_EQ("one.example.com", match->getName());

    // Verify that an FQDN with no match, returns the wild card domain.
    EXPECT_TRUE(cfg_mgr_->matchForward("shouldbe.wildcard", match));
    EXPECT_EQ("*", match->getName());

    // Verify that an attempt to match an empty FQDN throws.
    ASSERT_THROW(cfg_mgr_->matchForward("", match), D2CfgError);
}

/// @brief Tests domain matching when there is no wild card domain.
/// This test verifies that matches are found only for FQDNs that match
/// some or all of a domain name.  FQDNs without matches should not return
/// a match.
TEST_F(D2CfgMgrTest, matchNoWildcard) {
    // Create a configuration with one domain, one sub-domain, and NO wild card.
    std::string config = "{ "
                        "\"ip-address\" : \"192.168.1.33\" , "
                        "\"port\" : 88 , "
                        "\"tsig-keys\": [] ,"
                        "\"forward-ddns\" : {"
                        "\"ddns-domains\": [ "
                        "{ \"name\": \"example.com\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.0.1\" } "
                        "  ] } "
                        ", "
                        "{ \"name\": \"one.example.com\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.0.2\" } "
                        "  ] } "
                        "] }, "
                        "\"reverse-ddns\" : {} "
                        " }";

    // Verify that we can parse the configuration.
    RUN_CONFIG_OK(config);

    // Verify that the D2 context can be retrieved and is not null.
    D2CfgContextPtr context;
    ASSERT_NO_THROW(context = cfg_mgr_->getD2CfgContext());

    DdnsDomainPtr match;
    // Verify that full or partial matches, still match.
    EXPECT_TRUE(cfg_mgr_->matchForward("example.com", match));
    EXPECT_EQ("example.com", match->getName());

    EXPECT_TRUE(cfg_mgr_->matchForward("blue.example.com", match));
    EXPECT_EQ("example.com", match->getName());

    EXPECT_TRUE(cfg_mgr_->matchForward("red.one.example.com", match));
    EXPECT_EQ("one.example.com", match->getName());

    // Verify that a FQDN with no match, fails to match.
    EXPECT_FALSE(cfg_mgr_->matchForward("shouldbe.wildcard", match));
}

/// @brief Tests domain matching when there is ONLY a wild card domain.
/// This test verifies that any FQDN matches the wild card.
TEST_F(D2CfgMgrTest, matchAll) {
    std::string config = "{ "
                        "\"ip-address\" : \"192.168.1.33\" , "
                        "\"port\" : 88 , "
                        "\"tsig-keys\": [] ,"
                        "\"forward-ddns\" : {"
                        "\"ddns-domains\": [ "
                        "{ \"name\": \"*\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.0.1\" } "
                        "  ] } "
                        "] }, "
                        "\"reverse-ddns\" : {} "
                        "}";

    // Verify that we can parse the configuration.
    RUN_CONFIG_OK(config);

    // Verify that the D2 context can be retrieved and is not null.
    D2CfgContextPtr context;
    ASSERT_NO_THROW(context = cfg_mgr_->getD2CfgContext());

    // Verify that wild card domain is returned for any FQDN.
    DdnsDomainPtr match;
    EXPECT_TRUE(cfg_mgr_->matchForward("example.com", match));
    EXPECT_EQ("*", match->getName());
    EXPECT_TRUE(cfg_mgr_->matchForward("shouldbe.wildcard", match));
    EXPECT_EQ("*", match->getName());

    // Verify that an attempt to match an empty FQDN still throws.
    ASSERT_THROW(cfg_mgr_->matchReverse("", match), D2CfgError);

}

/// @brief Tests the basics of the D2CfgMgr reverse FQDN-domain matching
/// This test uses a valid configuration to exercise the D2CfgMgr's
/// reverse FQDN-to-domain matching.
/// It verifies that:
/// 1. Given an FQDN which exactly matches a domain's name, that domain is
/// returned as match.
/// 2. Given a FQDN for sub-domain in the list, returns the proper match.
/// 3. Given a FQDN that matches no domain name, returns the wild card domain
/// as a match.
TEST_F(D2CfgMgrTest, matchReverse) {
    std::string config = "{ "
                        "\"ip-address\" : \"192.168.1.33\" , "
                        "\"port\" : 88 , "
                        "\"tsig-keys\": [] ,"
                        "\"forward-ddns\" : {}, "
                        "\"reverse-ddns\" : {"
                        "\"ddns-domains\": [ "
                        "{ \"name\": \"5.100.168.192.in-addr.arpa.\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.0.1\" } "
                        "  ] }, "
                        "{ \"name\": \"100.200.192.in-addr.arpa.\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.0.1\" } "
                        "  ] }, "
                        "{ \"name\": \"170.192.in-addr.arpa.\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.0.1\" } "
                        "  ] }, "
                        // Note mixed case to test case insensitivity.
                        "{ \"name\": \"2.0.3.0.8.b.d.0.1.0.0.2.IP6.ARPA.\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.0.1\" } "
                        "  ] },"
                        "{ \"name\": \"*\" , "
                        "  \"dns-servers\" : [ "
                        "  { \"ip-address\": \"127.0.0.1\" } "
                        "  ] } "
                        "] } }";

    // Verify that we can parse the configuration.
    RUN_CONFIG_OK(config);

    // Verify that the D2 context can be retrieved and is not null.
    D2CfgContextPtr context;
    ASSERT_NO_THROW(context = cfg_mgr_->getD2CfgContext());

    // Test directional update flags.
    EXPECT_FALSE(cfg_mgr_->forwardUpdatesEnabled());
    EXPECT_TRUE(cfg_mgr_->reverseUpdatesEnabled());

    DdnsDomainPtr match;

    // Verify an exact match.
    EXPECT_TRUE(cfg_mgr_->matchReverse("192.168.100.5", match));
    EXPECT_EQ("5.100.168.192.in-addr.arpa.", match->getName());

    // Verify a sub-domain match.
    EXPECT_TRUE(cfg_mgr_->matchReverse("192.200.100.27", match));
    EXPECT_EQ("100.200.192.in-addr.arpa.", match->getName());

    // Verify a sub-domain match.
    EXPECT_TRUE(cfg_mgr_->matchReverse("192.170.50.30", match));
    EXPECT_EQ("170.192.in-addr.arpa.", match->getName());

    // Verify a wild card match.
    EXPECT_TRUE(cfg_mgr_->matchReverse("1.1.1.1", match));
    EXPECT_EQ("*", match->getName());

    // Verify a IPv6 match.
    EXPECT_TRUE(cfg_mgr_->matchReverse("2001:db8:302:99::",match));
    EXPECT_EQ("2.0.3.0.8.b.d.0.1.0.0.2.IP6.ARPA.", match->getName());

    // Verify a IPv6 wild card match.
    EXPECT_TRUE(cfg_mgr_->matchReverse("2001:db8:99:302::",match));
    EXPECT_EQ("*", match->getName());

    // Verify that an attempt to match an invalid IP address throws.
    ASSERT_THROW(cfg_mgr_->matchReverse("", match), D2CfgError);
}

/// @brief Tests D2 config parsing against a wide range of config permutations.
///
/// It tests for both syntax errors that the JSON parsing (D2ParserContext)
/// should detect as well as post-JSON parsing logic errors generated by
/// the Element parsers (i.e...SimpleParser/DhcpParser derivations)
///
///
/// It iterates over all of the test configurations described in given file.
/// The file content is JSON specialized to this test. The format of the file
/// is:
///
/// @code
/// # The file must open with a list. It's name is arbitrary.
///
/// { "test_list" :
/// [
///
/// #    Test one starts here:
///      {
///
/// #    Each test has:
/// #      1. description - optional text description
/// #      2. syntax-error - error JSON parser should emit (omit if none)
/// #      3. logic-error - error element parser(s) should emit (omit if none)
/// #      4. data - configuration text to parse
/// #
///      "description" : "<text describing test>",
///      "syntax-error" : "<exact text from JSON parser including position>" ,
///      "logic-error" : "<exact text from element parser including position>" ,
///      "data" :
///          {
/// #        configuration elements here
///          "bool_val" : false,
///          "some_map" :  {}
/// #         :
///          }
///      }
///
/// #    Next test would start here
///      ,
///      {
///      }
///
/// ]}
///
/// @endcode
///
/// (The file supports comments per Element::fromJSONFile())
///
TEST_F(D2CfgMgrTest, configPermutations) {
    std::string test_file = testDataFile("d2_cfg_tests.json");
    isc::data::ConstElementPtr tests;

    // Read contents of the file and parse it as JSON. Note it must contain
    // all valid JSON, we aren't testing JSON parsing.
    try {
        tests = isc::data::Element::fromJSONFile(test_file, true);
    } catch (const std::exception& ex) {
        FAIL() << "ERROR parsing file : " << test_file << " : " << ex.what();
    }

    // Read in each test For each test, read:
    //
    //  1. description - optional text description
    //  2. syntax-error or logic-error or neither
    //  3. data - configuration text to parse
    //  4. convert data into JSON text
    //  5. submit JSON for parsing
    isc::data::ConstElementPtr test;
    ASSERT_TRUE(tests->get("test-list"));
    BOOST_FOREACH(test, tests->get("test-list")->listValue()) {
        // Grab the description.
        std::string description = "<no desc>";
        isc::data::ConstElementPtr elem = test->get("description");
        if (elem) {
            elem->getValue(description);
        }

        // Grab the expected error message, if there is one.
        std::string expected_error = "";
        RunConfigMode mode = NO_ERROR;
        elem = test->get("syntax-error");
        if (elem) {
            elem->getValue(expected_error);
            mode = SYNTAX_ERROR;
        } else {
            elem = test->get("logic-error");
            if (elem) {
                elem->getValue(expected_error);
                mode = LOGIC_ERROR;
            }
        }

        // Grab the test's configuration data.
        isc::data::ConstElementPtr data = test->get("data");
        ASSERT_TRUE(data) << "No data for test: "
                          << " : " << test->getPosition();

        // Convert the test data back to JSON text, then submit it for parsing.
        stringstream os;
        data->toJSON(os);
        EXPECT_TRUE(runConfigOrFail(os.str(), mode, expected_error))
            << " failed for test: " << test->getPosition() << std::endl;
    }
}


} // end of anonymous namespace
