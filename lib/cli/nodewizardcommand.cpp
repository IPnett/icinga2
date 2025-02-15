/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012-2016 Icinga Development Team (https://www.icinga.org/)  *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#include "cli/nodewizardcommand.hpp"
#include "cli/nodeutility.hpp"
#include "cli/pkiutility.hpp"
#include "cli/featureutility.hpp"
#include "cli/apisetuputility.hpp"
#include "base/logger.hpp"
#include "base/console.hpp"
#include "base/application.hpp"
#include "base/tlsutility.hpp"
#include "base/scriptglobal.hpp"
#include "base/exception.hpp"
#include <boost/foreach.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>

using namespace icinga;
namespace po = boost::program_options;

REGISTER_CLICOMMAND("node/wizard", NodeWizardCommand);

String NodeWizardCommand::GetDescription(void) const
{
	return "Wizard for Icinga 2 node setup.";
}

String NodeWizardCommand::GetShortDescription(void) const
{
	return "wizard for node setup";
}

ImpersonationLevel NodeWizardCommand::GetImpersonationLevel(void) const
{
	return ImpersonateRoot;
}

int NodeWizardCommand::GetMaxArguments(void) const
{
	return -1;
}

/**
 * The entry point for the "node wizard" CLI command.
 *
 * @returns An exit status.
 */
int NodeWizardCommand::Run(const boost::program_options::variables_map& vm,
    const std::vector<std::string>& ap) const
{
	/*
	 * The wizard will get all information from the user,
	 * and then call all required functions.
	 */

	std::cout << ConsoleColorTag(Console_Bold | Console_ForegroundBlue)
	    << "Welcome to the Icinga 2 Setup Wizard!\n"
	    << "\n"
	    << "We'll guide you through all required configuration details.\n"
	    << "\n"
	    << "\n\n" << ConsoleColorTag(Console_Normal);

	//TODO: Add sort of bash completion to path input?

	/* 0. master or node setup?
	 * 1. Ticket
	 * 2. Master information for autosigning
	 * 3. Trusted cert location
	 * 4. CN to use (defaults to FQDN)
	 * 5. Local CA
	 * 6. New self signed certificate
	 * 7. Request signed certificate from master
	 * 8. copy key information to /etc/icinga2/pki
	 * 9. enable ApiListener feature
	 * 10. generate zones.conf with endpoints and zone objects
	 * 11. set NodeName = cn in constants.conf
	 * 12. reload icinga2, or tell the user to
	 */

	std::string answer;
	bool is_node_setup = true;

	/* master or node setup */
	std::cout << ConsoleColorTag(Console_Bold)
	    << "Please specify if this is a satellite setup "
	    << "('n' installs a master setup)" << ConsoleColorTag(Console_Normal)
	    << " [Y/n]: ";
	std::getline (std::cin, answer);

	boost::algorithm::to_lower(answer);

	String choice = answer;

	if (choice.Contains("n"))
		is_node_setup = false;

	if (is_node_setup) {
		/* node setup part */
		std::cout << "Starting the Node setup routine...\n";

		/* CN */
		std::cout << ConsoleColorTag(Console_Bold)
		    << "Please specifiy the common name (CN)"
		    << ConsoleColorTag(Console_Normal)
		    << " [" << Utility::GetFQDN() << "]: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		if (answer.empty())
			answer = Utility::GetFQDN();

		String cn = answer;
		cn = cn.Trim();

		std::vector<std::string> endpoints;

		String endpoint_buffer;

		std::cout << ConsoleColorTag(Console_Bold)
		    << "Please specify the master endpoint(s) this node should connect to:"
		    << ConsoleColorTag(Console_Normal) << "\n";
		String master_endpoint_name;

wizard_endpoint_loop_start:

		std::cout << ConsoleColorTag(Console_Bold)
		    << "Master Common Name" << ConsoleColorTag(Console_Normal)
		    << " (CN from your master setup): ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		if (answer.empty()) {
			Log(LogWarning, "cli", "Master CN is required! Please retry.");
			goto wizard_endpoint_loop_start;
		}

		endpoint_buffer = answer;
		endpoint_buffer = endpoint_buffer.Trim();

		std::cout << "Do you want to establish a connection to the master "
		    << ConsoleColorTag(Console_Bold) << "from this node?"
		    << ConsoleColorTag(Console_Normal) << " [Y/n]: ";

		std::getline (std::cin, answer);
		boost::algorithm::to_lower(answer);
		choice = answer;

		String tmpPort = "5665";

		if (choice.Contains("n")) {
			Log(LogWarning, "cli", "Node to master connection setup skipped");
			std::cout << "Connection setup skipped. Please configure your master to connect to this node.\n";
		} else  {
			std::cout << ConsoleColorTag(Console_Bold)
			    << "Please fill out the master connection information:"
			    << ConsoleColorTag(Console_Normal) << "\n"
			    << ConsoleColorTag(Console_Bold) << "Master endpoint host"
			    << ConsoleColorTag(Console_Normal) << " (Your master's IP address or FQDN): ";

			std::getline(std::cin, answer);
			boost::algorithm::to_lower(answer);

			if (answer.empty()) {
				Log(LogWarning, "cli", "Please enter the master's endpoint connection information.");
				goto wizard_endpoint_loop_start;
			}

			String tmp = answer;
			tmp = tmp.Trim();

			endpoint_buffer += "," + tmp;
			master_endpoint_name = tmp; //store the endpoint name for later

			std::cout << ConsoleColorTag(Console_Bold)
			     << "Master endpoint port" << ConsoleColorTag(Console_Normal)
			     << " [" << tmpPort << "]: ";

			std::getline(std::cin, answer);
			boost::algorithm::to_lower(answer);

			if (!answer.empty())
				tmpPort = answer;

			endpoint_buffer += "," + tmpPort.Trim();
		}

		endpoints.push_back(endpoint_buffer);

		std::cout << ConsoleColorTag(Console_Bold) << "Add more master endpoints?"
		    << ConsoleColorTag(Console_Normal) << " [y/N]: ";
		std::getline (std::cin, answer);

		boost::algorithm::to_lower(answer);

		String choice = answer;

		if (choice.Contains("y"))
			goto wizard_endpoint_loop_start;

		std::cout << ConsoleColorTag(Console_Bold)
		    << "Please specify the master connection for CSR auto-signing"
		    << ConsoleColorTag(Console_Normal) << " (defaults to master endpoint host):\n";

wizard_master_host:
		std::cout << ConsoleColorTag(Console_Bold) << "Host"
		    << ConsoleColorTag(Console_Normal) << " [" << master_endpoint_name << "]: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		if (answer.empty() && !master_endpoint_name.IsEmpty())
			answer = master_endpoint_name;

		if (answer.empty() && master_endpoint_name.IsEmpty())
			goto wizard_master_host;

		String master_host = answer;
		master_host = master_host.Trim();

		std::cout << ConsoleColorTag(Console_Bold) << "Port"
		    << ConsoleColorTag(Console_Normal) << " [" << tmpPort << "]: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		if (!answer.empty())
			tmpPort = answer;

		String master_port = tmpPort;
		master_port = master_port.Trim();

		/* workaround for fetching the master cert */
		String pki_path = PkiUtility::GetPkiPath();
		Utility::MkDirP(pki_path, 0700);

		String user = ScriptGlobal::Get("RunAsUser");
		String group = ScriptGlobal::Get("RunAsGroup");

		if (!Utility::SetFileOwnership(pki_path, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user
			    << "' group '" << group
			    << "' on file '" << pki_path << "'. Verify it yourself!";
		}

		String node_cert = pki_path + "/" + cn + ".crt";
		String node_key = pki_path + "/" + cn + ".key";

		if (Utility::PathExists(node_key))
			NodeUtility::CreateBackupFile(node_key, true);
		if (Utility::PathExists(node_cert))
			NodeUtility::CreateBackupFile(node_cert);

		if (PkiUtility::NewCert(cn, node_key, Empty, node_cert) > 0) {
			Log(LogCritical, "cli")
			    << "Failed to create new self-signed certificate for CN '"
			    << cn << "'. Please try again.";
			return 1;
		}

		/* fix permissions: root -> icinga daemon user */
		if (!Utility::SetFileOwnership(node_key, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user
			    << "' group '" << group
			    << "' on file '" << node_key << "'. Verify it yourself!";
		}

		//save-cert and store the master certificate somewhere
		Log(LogInformation, "cli")
		    << "Fetching public certificate from master ("
		    << master_host << ", " << master_port << "):\n";

		boost::shared_ptr<X509> trustedcert = PkiUtility::FetchCert(master_host, master_port);
		if (!trustedcert) {
			Log(LogCritical, "cli", "Peer did not present a valid certificate.");
			return 1;
		}

		std::cout << ConsoleColorTag(Console_Bold) << "Certificate information:\n"
		    << ConsoleColorTag(Console_Normal) << PkiUtility::GetCertificateInformation(trustedcert)
		    << ConsoleColorTag(Console_Bold) << "\nIs this information correct?"
		    << ConsoleColorTag(Console_Normal) << " [y/N]: ";

		std::getline (std::cin, answer);
		boost::algorithm::to_lower(answer);
		if (answer != "y") {
			Log(LogWarning, "cli", "Process aborted.");
			return 1;
		}

		Log(LogInformation, "cli", "Received trusted master certificate.\n");

wizard_ticket:
		std::cout << ConsoleColorTag(Console_Bold)
		    << "Please specify the request ticket generated on your Icinga 2 master."
		    << ConsoleColorTag(Console_Normal) << "\n"
		    << " (Hint: # icinga2 pki ticket --cn '" << cn << "'): ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		if (answer.empty())
			goto wizard_ticket;

		String ticket = answer;
		ticket = ticket.Trim();

		Log(LogInformation, "cli")
		    << "Requesting certificate with ticket '" << ticket << "'.\n";

		String target_ca = pki_path + "/ca.crt";

		if (Utility::PathExists(target_ca))
			NodeUtility::CreateBackupFile(target_ca);
		if (Utility::PathExists(node_cert))
			NodeUtility::CreateBackupFile(node_cert);

		if (PkiUtility::RequestCertificate(master_host, master_port, node_key,
		    node_cert, target_ca, trustedcert, ticket) > 0) {
			Log(LogCritical, "cli")
			    << "Failed to fetch signed certificate from master '"
			    << master_host << ", "
			    << master_port <<"'. Please try again.";
			goto wizard_ticket;
		}

		/* fix permissions (again) when updating the signed certificate */
		if (!Utility::SetFileOwnership(node_cert, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user
			    << "' group '" << group << "' on file '"
			    << node_cert << "'. Verify it yourself!";
		}

		/* apilistener config */
		std::cout << ConsoleColorTag(Console_Bold)
		    << "Please specify the API bind host/port"
		    << ConsoleColorTag(Console_Normal) << " (optional):\n"
		    << ConsoleColorTag(Console_Bold) << "Bind Host"
		    << ConsoleColorTag(Console_Normal) << " []: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		String bind_host = answer;
		bind_host = bind_host.Trim();

		std::cout << "Bind Port []: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		String bind_port = answer;
		bind_port = bind_port.Trim();

		std::cout << ConsoleColorTag(Console_Bold)
		    << "Accept config from master?" << ConsoleColorTag(Console_Normal)
		    << " [y/N]: ";
		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);
		choice = answer;

		String accept_config = choice.Contains("y") ? "true" : "false";

		std::cout << ConsoleColorTag(Console_Bold)
		    << "Accept commands from master?" << ConsoleColorTag(Console_Normal)
		    << " [y/N]: ";
		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);
		choice = answer;

		String accept_commands = choice.Contains("y") ? "true" : "false";

		/* disable the notifications feature on client nodes */
		Log(LogInformation, "cli", "Disabling the Notification feature.");

		std::vector<std::string> disable;
		disable.push_back("notification");
		FeatureUtility::DisableFeatures(disable);

		Log(LogInformation, "cli", "Enabling the Apilistener feature.");

		std::vector<std::string> enable;
		enable.push_back("api");
		FeatureUtility::EnableFeatures(enable);

		String apipath = FeatureUtility::GetFeaturesAvailablePath() + "/api.conf";
		NodeUtility::CreateBackupFile(apipath);

		std::fstream fp;
		String tempApiPath = Utility::CreateTempFile(apipath + ".XXXXXX", 0640, fp);

		fp << "/**\n"
		    << " * The API listener is used for distributed monitoring setups.\n"
		    << " */\n"
		    << "object ApiListener \"api\" {\n"
		    << "  cert_path = SysconfDir + \"/icinga2/pki/\" + NodeName + \".crt\"\n"
		    << "  key_path = SysconfDir + \"/icinga2/pki/\" + NodeName + \".key\"\n"
		    << "  ca_path = SysconfDir + \"/icinga2/pki/ca.crt\"\n"
		    << "\n"
		    << "  accept_config = " << accept_config << "\n"
		    << "  accept_commands = " << accept_commands << "\n";

		if (!bind_host.IsEmpty())
			fp << "  bind_host = \"" << bind_host << "\"\n";
		if (!bind_port.IsEmpty())
			fp << "  bind_port = " << bind_port << "\n";

		fp << "\n"
		    << "  ticket_salt = TicketSalt\n"
		    << "}\n";

		fp.close();

#ifdef _WIN32
		_unlink(apipath.CStr());
#endif /* _WIN32 */

		if (rename(tempApiPath.CStr(), apipath.CStr()) < 0) {
			BOOST_THROW_EXCEPTION(posix_error()
			    << boost::errinfo_api_function("rename")
			    << boost::errinfo_errno(errno)
			    << boost::errinfo_file_name(tempApiPath));
		}

		/* apilistener config */
		Log(LogInformation, "cli", "Generating local zones.conf.");

		NodeUtility::GenerateNodeIcingaConfig(endpoints);

		if (cn != Utility::GetFQDN()) {
			Log(LogWarning, "cli")
			    << "CN '" << cn << "' does not match the default FQDN '"
			    << Utility::GetFQDN() << "'. Requires update for NodeName constant in constants.conf!";
		}

		Log(LogInformation, "cli", "Updating constants.conf.");

		String constants_file = Application::GetSysconfDir() + "/icinga2/constants.conf";

		NodeUtility::CreateBackupFile(constants_file);

		NodeUtility::UpdateConstant("NodeName", cn);
		NodeUtility::UpdateConstant("ZoneName", cn);
	} else {
		/* master setup */
		std::cout << ConsoleColorTag(Console_Bold) << "Starting the Master setup routine...\n";

		/* CN */
		std::cout << ConsoleColorTag(Console_Bold)
		    << "Please specifiy the common name" << ConsoleColorTag(Console_Normal)
		    << " (CN) [" << Utility::GetFQDN() << "]: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		if (answer.empty())
			answer = Utility::GetFQDN();

		String cn = answer;
		cn = cn.Trim();

		/* check whether the user wants to generate a new certificate or not */
		String existing_path = PkiUtility::GetPkiPath() + "/" + cn + ".crt";

		std::cout << ConsoleColorTag(Console_Normal)
		    << "Checking for existing certificates for common name '" << cn << "'...\n";

		if (Utility::PathExists(existing_path)) {
			std::cout << "Certificate '" << existing_path << "' for CN '"
			    << cn << "' already existing. Skipping certificate generation.\n";
		} else {
			std::cout << "Certificates not yet generated. Running 'api setup' now.\n";
			ApiSetupUtility::SetupMasterCertificates(cn);
		}

		std::cout << ConsoleColorTag(Console_Bold)
		    << "Generating master configuration for Icinga 2.\n"
		    << ConsoleColorTag(Console_Normal);
		ApiSetupUtility::SetupMasterApiUser();

		if (!FeatureUtility::CheckFeatureEnabled("api"))
			ApiSetupUtility::SetupMasterEnableApi();
		else
			std::cout << "'api' feature already enabled.\n";

		NodeUtility::GenerateNodeMasterIcingaConfig();

		/* apilistener config */
		std::cout << ConsoleColorTag(Console_Bold)
		    << "Please specify the API bind host/port (optional):\n";
		std::cout << ConsoleColorTag(Console_Bold)
		    << "Bind Host" << ConsoleColorTag(Console_Normal) << " []: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		String bind_host = answer;
		bind_host = bind_host.Trim();

		std::cout << ConsoleColorTag(Console_Bold)
		    << "Bind Port" << ConsoleColorTag(Console_Normal) << " []: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		String bind_port = answer;
		bind_port = bind_port.Trim();

		/* api feature is always enabled, check above */
		String apipath = FeatureUtility::GetFeaturesAvailablePath() + "/api.conf";
		NodeUtility::CreateBackupFile(apipath);


		std::fstream fp;
		String tempApiPath = Utility::CreateTempFile(apipath + ".XXXXXX", 0640, fp);

		fp << "/**\n"
		    << " * The API listener is used for distributed monitoring setups.\n"
		    << " */\n"
		    << "object ApiListener \"api\" {\n"
		    << "  cert_path = SysconfDir + \"/icinga2/pki/\" + NodeName + \".crt\"\n"
		    << "  key_path = SysconfDir + \"/icinga2/pki/\" + NodeName + \".key\"\n"
		    << "  ca_path = SysconfDir + \"/icinga2/pki/ca.crt\"\n";

		if (!bind_host.IsEmpty())
			fp << "  bind_host = \"" << bind_host << "\"\n";
		if (!bind_port.IsEmpty())
			fp << "  bind_port = " << bind_port << "\n";

		fp << "\n"
		    << "  ticket_salt = TicketSalt\n"
		    << "}\n";

		fp.close();

#ifdef _WIN32
		_unlink(apipath.CStr());
#endif /* _WIN32 */

		if (rename(tempApiPath.CStr(), apipath.CStr()) < 0) {
			BOOST_THROW_EXCEPTION(posix_error()
			    << boost::errinfo_api_function("rename")
			    << boost::errinfo_errno(errno)
			    << boost::errinfo_file_name(tempApiPath));
		}

		/* update constants.conf with NodeName = CN + TicketSalt = random value */
		if (cn != Utility::GetFQDN()) {
			Log(LogWarning, "cli")
			    << "CN '" << cn << "' does not match the default FQDN '"
			    << Utility::GetFQDN() << "'. Requires update for NodeName constant in constants.conf!";
		}

		Log(LogInformation, "cli", "Updating constants.conf.");

		String constants_file = Application::GetSysconfDir() + "/icinga2/constants.conf";

		NodeUtility::CreateBackupFile(constants_file);

		NodeUtility::UpdateConstant("NodeName", cn);
		NodeUtility::UpdateConstant("ZoneName", cn);

		String salt = RandomString(16);

		NodeUtility::UpdateConstant("TicketSalt", salt);
	}

	std::cout << "Done.\n\n";

	std::cout << "Now restart your Icinga 2 daemon to finish the installation!\n\n";

	return 0;
}
