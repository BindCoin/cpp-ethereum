#pragma once

/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file KeyAux.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 * CLI module for key management.
 */

#include <thread>
#include <chrono>
#include <fstream>
#include <iostream>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/trim_all.hpp>
#include <libdevcore/SHA3.h>
#include <libdevcore/FileSystem.h>
#include <libethcore/KeyManager.h>
#include <libethcore/ICAP.h>
#include "BuildInfo.h"
using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace boost::algorithm;

#undef RETURN

class BadArgument: public Exception {};

string getAccountPassword(KeyManager& keyManager, Address const& a)
{
	return getPassword("Enter password for address " + keyManager.accountDetails()[a].first + " (" + a.abridged() + "; hint:" + keyManager.accountDetails()[a].second + "): ");
}

string createPassword(std::string const& _prompt)
{
	string ret;
	while (true)
	{
		ret = getPassword(_prompt);
		string confirm = getPassword("Please confirm the password by entering it again: ");
		if (ret == confirm)
			break;
		cout << "Passwords were different. Try again." << endl;
	}
	return ret;
//	cout << "Enter a hint to help you remember this password: " << flush;
//	cin >> hint;
//	return make_pair(ret, hint);
}

pair<string, string> createPassword(KeyManager& _keyManager, std::string const& _prompt)
{
	string pass;
	while (true)
	{
		pass = getPassword(_prompt);
		string confirm = getPassword("Please confirm the password by entering it again: ");
		if (pass == confirm)
			break;
		cout << "Passwords were different. Try again." << endl;
	}
	string hint;
	if (!_keyManager.haveHint(pass))
	{
		cout << "Enter a hint to help you remember this password: " << flush;
		cin >> hint;
	}
	return make_pair(pass, hint);
}

class KeyCLI
{
public:
	enum class OperationMode
	{
		None,
		ListBare,
		NewBare,
		ImportBare,
		ExportBare,
		RecodeBare,
		FirstWallet,
		CreateWallet,
		List = FirstWallet,
		New,
		Import,
		Export,
		Recode,
		Kill
	};

	KeyCLI(OperationMode _mode = OperationMode::None): m_mode(_mode) {}

	bool interpretOption(int& i, int argc, char** argv)
	{
		string arg = argv[i];
		if (arg == "-n" || arg == "--new")
			m_mode = OperationMode::New;
		else if (arg == "--wallet-path" && i + 1 < argc)
			m_walletPath = argv[++i];
		else if (arg == "--secrets-path" && i + 1 < argc)
			m_secretsPath = argv[++i];
		else if ((arg == "-m" || arg == "--master") && i + 1 < argc)
			m_masterPassword = argv[++i];
		else if (arg == "--unlock" && i + 1 < argc)
			m_unlocks.push_back(argv[++i]);
		else if (arg == "--lock" && i + 1 < argc)
			m_lock = argv[++i];
		else if (arg == "--kdf" && i + 1 < argc)
			m_kdf = argv[++i];
		else if (arg == "--kdf-param" && i + 2 < argc)
		{
			auto n = argv[++i];
			auto v = argv[++i];
			m_kdfParams[n] = v;
		}
		else if (arg == "--new-bare")
			m_mode = OperationMode::NewBare;
		else if (arg == "--import-bare")
			m_mode = OperationMode::ImportBare;
		else if (arg == "--list-bare")
			m_mode = OperationMode::ListBare;
		else if (arg == "--export-bare")
			m_mode = OperationMode::ExportBare;
		else if (arg == "--recode-bare")
			m_mode = OperationMode::RecodeBare;
		else if (arg == "--create-wallet")
			m_mode = OperationMode::CreateWallet;
		else if (arg == "--list")
			m_mode = OperationMode::List;
		else if ((arg == "-n" || arg == "--new") && i + 1 < argc)
		{
			m_mode = OperationMode::New;
			m_name = argv[++i];
		}
		else if ((arg == "-i" || arg == "--import") && i + 2 < argc)
		{
			m_mode = OperationMode::Import;
			m_inputs = strings(1, argv[++i]);
			m_name = argv[++i];
		}
		else if (arg == "--export")
			m_mode = OperationMode::Export;
		else if (arg == "--recode")
			m_mode = OperationMode::Recode;
		else if (arg == "--no-icap")
			m_icap = false;
		else if (m_mode == OperationMode::ImportBare || m_mode == OperationMode::Recode || m_mode == OperationMode::Export || m_mode == OperationMode::RecodeBare || m_mode == OperationMode::ExportBare)
			m_inputs.push_back(arg);
		else
			return false;
		return true;
	}

	KeyPair makeKey() const
	{
		KeyPair k(Secret::random());
		while (m_icap && k.address()[0])
			k = KeyPair(sha3(k.secret()));
		return k;
	}

	void execute()
	{
		if (m_mode == OperationMode::CreateWallet)
		{
			KeyManager wallet(m_walletPath, m_secretsPath);
			if (m_masterPassword.empty())
				m_masterPassword = createPassword("Please enter a MASTER password to protect your key store (make it strong!): ");
			if (m_masterPassword.empty())
				cerr << "Aborted (empty password not allowed)." << endl;
			else
				wallet.create(m_masterPassword);
		}
		else if (m_mode < OperationMode::FirstWallet)
		{
			SecretStore store(m_secretsPath);
			switch (m_mode)
			{
			case OperationMode::ListBare:
				for (h128 const& u: std::set<h128>() + store.keys())
					cout << toUUID(u) << endl;
				break;
			case OperationMode::NewBare:
			{
				if (m_lock.empty())
					m_lock = createPassword("Enter a password with which to secure this account: ");
				auto k = makeKey();
				store.importSecret(k.secret().asBytes(), m_lock);
				cout << "Created key " << k.address().abridged() << endl;
				cout << "Address: " << k.address().hex() << endl;
				cout << "ICAP: " << ICAP(k.address()).encoded() << endl;
				break;
			}
			case OperationMode::ImportBare:
				for (string const& i: m_inputs)
				{
					h128 u;
					bytes b;
					b = fromHex(i);
					if (b.size() != 32)
					{
						std::string s = contentsString(i);
						b = fromHex(s);
						if (b.size() != 32)
							u = store.importKey(i);
					}
					if (!u && b.size() == 32)
						u = store.importSecret(b, lockPassword(toAddress(Secret(b)).abridged()));
					else
					{
						cerr << "Cannot import " << i << " not a file or secret." << endl;
						continue;
					}
					cout << "Successfully imported " << i << " as " << toUUID(u);
				}
				break;
			case OperationMode::ExportBare: break;
			case OperationMode::RecodeBare:
				for (auto const& i: m_inputs)
				{
					h128 u = fromUUID(i);
					if (u)
						if (store.recode(u, lockPassword(toUUID(u)), [&](){ return getPassword("Enter password for key " + toUUID(u) + ": "); }, kdf()))
							cerr << "Re-encoded " << toUUID(u) << endl;
						else
							cerr << "Couldn't re-encode " << toUUID(u) << "; key corrupt or incorrect password supplied." << endl;
					else
						cerr << "Couldn't re-encode " << toUUID(u) << "; not found." << endl;
				}
			default: break;
			}
		}
		else
		{
			KeyManager wallet(m_walletPath, m_secretsPath);
			if (wallet.exists())
				while (true)
				{
					if (wallet.load(m_masterPassword))
						break;
					if (!m_masterPassword.empty())
					{
						cout << "Password invalid. Try again." << endl;
						m_masterPassword.clear();
					}
					m_masterPassword = getPassword("Please enter your MASTER password: ");
				}
			else
			{
				cerr << "Couldn't open wallet. Does it exist?" << endl;
				exit(-1);
			}
		}
	}

	std::string lockPassword(std::string const& _accountName)
	{
		return m_lock.empty() ? createPassword("Enter a password with which to secure account " + _accountName + ": ") : m_lock;
	}

	static void streamHelp(ostream& _out)
	{
		_out
			<< "Secret-store (\"bare\") operation modes:" << endl
			<< "    --list-bare  List all secret available in secret-store." << endl
			<< "    --new-bare  Generate and output a key without interacting with wallet and dump the JSON." << endl
			<< "    --import-bare [ <file>|<secret-hex> , ... ] Import keys from given sources." << endl
			<< "    --recode-bare [ <uuid>|<file> , ... ]  Decrypt and re-encrypt given keys." << endl
//			<< "    --export-bare [ <uuid> , ... ]  Export given keys." << endl
			<< "Secret-store configuration:" << endl
			<< "    --secrets-path <path>  Specify Web3 secret-store path (default: " << SecretStore::defaultPath() << ")" << endl
			<< endl
			<< "Wallet operating modes:" << endl
			<< "    -l,--list  List all keys available in wallet." << endl
			<< "    -n,--new <name>  Create a new key with given name and add it in the wallet." << endl
			<< "    -i,--import [<uuid>|<file>|<secret-hex>] <name>  Import keys from given source and place in wallet." << endl
			<< "    -e,--export [ <address>|<uuid> , ... ]  Export given keys." << endl
			<< "    -r,--recode [ <address>|<uuid>|<file> , ... ]  Decrypt and re-encrypt given keys." << endl
			<< "Wallet configuration:" << endl
			<< "    --create-wallet  Create an Ethereum master wallet." << endl
			<< "    --wallet-path <path>  Specify Ethereum wallet path (default: " << KeyManager::defaultPath() << ")" << endl
			<< "    -m, --master <password>  Specify wallet (master) password." << endl
			<< endl
			<< "Encryption configuration:" << endl
			<< "    --kdf <kdfname>  Specify KDF to use when encrypting (default: sc	rypt)" << endl
			<< "    --kdf-param <name> <value>  Specify a parameter for the KDF." << endl
//			<< "    --cipher <ciphername>  Specify cipher to use when encrypting (default: aes-128-ctr)" << endl
//			<< "    --cipher-param <name> <value>  Specify a parameter for the cipher." << endl
			<< "    --lock <password> <hint>  Specify password for when encrypting a (the) key." << endl
			<< endl
			<< "Decryption configuration:" << endl
			<< "    --unlock <password>  Specify password for a (the) key." << endl
			<< "Key generation configuration:" << endl
			<< "    --no-icap  Don't bother to make a direct-ICAP capable key." << endl
			;
	}

	static bool isTrue(std::string const& _m)
	{
		return _m == "on" || _m == "yes" || _m == "true" || _m == "1";
	}

	static bool isFalse(std::string const& _m)
	{
		return _m == "off" || _m == "no" || _m == "false" || _m == "0";
	}

private:
	KDF kdf() const { return m_kdf == "pbkdf2" ? KDF::PBKDF2_SHA256 : KDF::Scrypt; }

	/// Operating mode.
	OperationMode m_mode;

	/// Wallet stuff
	string m_secretsPath = SecretStore::defaultPath();
	string m_walletPath = KeyManager::defaultPath();

	/// Wallet password stuff
	string m_masterPassword;
	strings m_unlocks;
	string m_lock;
	bool m_icap = true;

	/// Creating
	string m_name;

	/// Importing
	strings m_inputs;

	string m_kdf = "scrypt";
	map<string, string> m_kdfParams;
//	string m_cipher;
//	map<string, string> m_cipherParams;
};
