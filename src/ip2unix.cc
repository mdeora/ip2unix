// SPDX-License-Identifier: LGPL-3.0-only
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <getopt.h>
#include <string>
#include <unistd.h>

#include "rules.hh"

extern char **environ;

static bool run_preload(std::vector<Rule> &rules, char *argv[])
{
    char self[PATH_MAX], *preload;
    ssize_t len;

    if ((len = readlink("/proc/self/exe", self, sizeof(self) - 1)) == -1) {
        perror("readlink(\"/proc/self/exe\")");
        return false;
    }

    self[len] = '\0';

    if ((preload = getenv("LD_PRELOAD")) != nullptr && *preload != '\0') {
        std::string new_preload = std::string(self) + ":" + preload;
        setenv("LD_PRELOAD", new_preload.c_str(), 1);
    } else {
        setenv("LD_PRELOAD", self, 1);
    }

    std::string encoded = encode_rules(rules);

    setenv("__IP2UNIX_RULES", encoded.c_str(), 1);

    if (execvpe(argv[0], argv, environ) == -1) {
        std::string err = "execvpe(\"" + std::string(argv[0]) + "\")";
        perror(err.c_str());
    }

    return false;
}

#define PROG "PROGRAM [ARGS...]"
#define COMMON "[-v...] [-p]"

static void print_usage(char *prog, FILE *fp)
{
    fprintf(fp, "Usage: %s " COMMON " -f RULES_FILE        " PROG "\n", prog);
    fprintf(fp, "       %s " COMMON " -F RULES_DATA        " PROG "\n", prog);
    fprintf(fp, "       %s " COMMON " -r RULE [-r RULE]... " PROG "\n", prog);
    fprintf(fp, "       %s " COMMON " -c -f RULES_FILE\n", prog);
    fprintf(fp, "       %s " COMMON " -c -F RULES_DATA\n", prog);
    fprintf(fp, "       %s " COMMON " -c -r RULE [-r RULE]...\n", prog);
    fprintf(fp, "       %s -h\n", prog);
    fprintf(fp, "       %s --version\n", prog);
    fputs("\nTurn IP sockets into Unix domain sockets for PROGRAM\n", fp);
    fputs("according to the rules specified by either the YAML file\n", fp);
    fputs("given by RULES_FILE, inline via RULES_DATA or by directly\n", fp);
    fputs("specifying one or more individual RULE arguments.\n", fp);
    fputs("\nOptions:\n", fp);
    fputs("  -h, --help        Show this usage\n",                     fp);
    fputs("      --version     Output version information and exit\n", fp);
    fputs("  -c, --check       Validate rules and exit\n",             fp);
    fputs("  -p, --print       Print out the table of rules\n",        fp);
    fputs("  -f, --rules-file  YAML/JSON file containing the rules\n", fp);
    fputs("  -F, --rules-data  Rules as inline YAML/JSON data\n",      fp);
    fputs("  -r, --rule        A single rule\n",                       fp);
    fputs("  -v, --verbose     Increase level of verbosity\n",         fp);
    fputs("\nSee ip2unix(1) for details about specifying rules.\n", fp);
}

static void print_version(void)
{
    fputs("ip2unix " VERSION "\n"
          "Copyright (C) 2018 aszlig\n"
          "This program is free software; you may redistribute it under\n"
          "the terms of the GNU Lesser General Public License version 3.\n",
          stdout);
}

int main(int argc, char *argv[])
{
    int c;
    char *self = argv[0];

    bool check_only = false;
    bool show_rules = false;
    unsigned int verbosity = 0;

    static struct option lopts[] = {
        {"help", no_argument, nullptr, 'h'},
        {"version", no_argument, nullptr, 'V'},
        {"check", no_argument, nullptr, 'c'},
        {"print", no_argument, nullptr, 'p'},
        {"rule", required_argument, nullptr, 'r'},
        {"rules-file", required_argument, nullptr, 'f'},
        {"rules-data", required_argument, nullptr, 'F'},
        {"verbose", no_argument, nullptr, 'v'},
        {nullptr, 0, nullptr, 0}
    };

    std::optional<std::string> rulefile = std::nullopt;
    std::optional<std::string> ruledata = std::nullopt;
    std::vector<std::string> rule_args;

    while ((c = getopt_long(argc, argv, "+hcpr:f:F:v",
                            lopts, nullptr)) != -1) {
        switch (c) {
            case 'h':
                print_usage(self, stdout);
                return EXIT_SUCCESS;

            case 'V':
                print_version();
                return EXIT_SUCCESS;

            case 'c':
                check_only = true;
                break;

            case 'p':
                show_rules = true;
                break;

            case 'r':
                rule_args.push_back(optarg);
                break;

            case 'f':
                rulefile = std::string(optarg);
                break;

            case 'F':
                ruledata = std::string(optarg);
                break;

            case 'v':
                verbosity++;
                break;

            default:
                fputc('\n', stderr);
                print_usage(self, stderr);
                return EXIT_FAILURE;
        }
    }

    if (!rule_args.empty() && (rulefile || ruledata)) {
        fprintf(stderr, "%s: Can't specify both direct rules and a rule"
                        " file.\n\n", self);
        print_usage(self, stderr);
        return EXIT_FAILURE;
    }

    if (rulefile && ruledata) {
        fprintf(stderr, "%s: Can't use a rule file path and inline rules"
                        " at the same time.\n\n", self);
        print_usage(self, stderr);
        return EXIT_FAILURE;
    }

    std::vector<Rule> rules;

    if (!rule_args.empty()) {
        size_t rulepos = 0;
        for (auto arg : rule_args) {
            auto result = parse_rule_arg(++rulepos, arg);
            if (result)
                rules.push_back(result.value());
            else
                return EXIT_FAILURE;
        }
    } else if (rulefile) {
        auto result = parse_rules(rulefile.value(), true);
        if (!result) return EXIT_FAILURE;
        rules = result.value();
    } else if (ruledata) {
        auto result = parse_rules(ruledata.value(), false);
        if (!result) return EXIT_FAILURE;
        rules = result.value();
    } else {
        fprintf(stderr, "%s: You need to either specify a rule file with '-f'"
                        " or '-F' (for inline content) or directly specify"
                        " rules via '-r'.\n\n", self);
        print_usage(self, stderr);
        return EXIT_FAILURE;
    }

    if (show_rules)
        print_rules(rules, check_only ? std::cout : std::cerr);
    if (check_only)
        return EXIT_SUCCESS;

    argc -= optind;
    argv += optind;

    if (argc >= 1) {
        if (verbosity > 0) {
            setenv("__IP2UNIX_VERBOSITY",
                   std::to_string(verbosity).c_str(), 1);
        }
        run_preload(rules, argv);
    } else {
        fprintf(stderr, "%s: No program to execute specified.\n", self);
        print_usage(self, stderr);
    }

    return EXIT_FAILURE;
}
