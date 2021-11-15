//
// Copyright 2019 ZetaSQL Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

// Tool for running a query against a Catalog constructed from various input
// sources. Also serves as a demo of the PreparedQuery API.
//
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

#include "zetasql/base/logging.h"
#include "zetasql/base/status.h"
#include "zetasql/public/sql_formatter.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/strip.h"
#include "absl/strings/str_join.h"

ABSL_FLAG(std::string, tokens, "",
          "Filter the tokens to print (k = Keywords, i = Identifiers, v = Values, c = Comments).");

int format(const std::filesystem::path& file_path) {
  std::string formatted;
  if (file_path.extension() == ".bq" || file_path.extension() == ".sql") {
    std::cout << "formatting " << file_path << "..." << std::endl;
    std::ifstream file(file_path, std::ios::in);
    std::string sql(std::istreambuf_iterator<char>(file), {});
    const absl::Status status = zetasql::FormatSql(sql, &formatted);
    if (status.ok()) {
      std::ofstream out(file_path);
      out << formatted;
      if (formatted != sql) {
        std::cout << "successfully formatted " << file_path << "!" << std::endl;
        return 1;
      }
    } else {
      std::cout << "ERROR: " << status << std::endl;
      return 1;
    }
    std::cout << file_path << " is already formatted!" << std::endl;
  }
  return 0;
}

int show_tokens(const std::filesystem::path& file_path, const std::string& tokens_filter, std::string* tokens_output) {
  if (file_path.extension() == ".bq" || file_path.extension() == ".sql") {
    const absl::Status status = zetasql::ShowTokens(file_path, tokens_filter, tokens_output);
    if (status.ok()) {
      return EXIT_SUCCESS;
    } else {
      std::string message = std::string{status.message()};
      message = zetasql::EscapeValue(message);
      *tokens_output = "{\"filename\":\"" + file_path.string() + "\",\"error\":\"" + message + "\"}";
      return EXIT_FAILURE;
    }
  }
  return 0;
}

// format formats all sql files in specified directory and returns code 0
// if all files are formatted and 1 if error occurs or any file is formatted.
int main(int argc, char* argv[]) {

  const char kUsage[] =
      "Usage: format [--print-tokens=kivc] <directory paths...>\n";

  std::vector<char*> args = absl::ParseCommandLine(argc, argv);
  if (argc <= 1) {
    ZETASQL_LOG(QFATAL) << kUsage;
  }

  // check if has print tokens arguments
  std::string flags_tokens = absl::GetFlag(FLAGS_tokens);

  std::vector<char*> remaining_args(args.begin() + 1, args.end());

  int rc = 0;
  for (const auto& path : remaining_args) {

    // ignoring flag arguments
    std::string argument = path;
    if (argument.rfind("--", 0) == 0) {
      continue;
    }

    // argument is a file ...   
    if (std::filesystem::is_regular_file(path)) {
      std::filesystem::path file_path(path);

      // show the token contents
      if (flags_tokens != "") {
        std::string tokens_content = "";
        rc = show_tokens(file_path, flags_tokens, &tokens_content);
        if (tokens_content != "") {
          std::cout << "[" << tokens_content << "]" << std::endl;
        }
        return rc;
      } 
    }

    // argument is a directory ...
    std::filesystem::recursive_directory_iterator file_path(path, std::filesystem::directory_options::skip_permission_denied);
    std::filesystem::recursive_directory_iterator end;
    std::error_code err;

    // show the token contents ...
    if (flags_tokens != "") {
      std::string result = "[";
      std::string separator = "";
      
      for (; file_path != end; file_path.increment(err)) {
        if (err) {
          result += separator;
          result += "{\"filename\":\"" + file_path->path().string() + "\",\"error\":\"" + err.message() + "\"}";
          separator = ", ";
        } else {
          std::string tokens_content = "";
          rc |= show_tokens(file_path->path(), flags_tokens, &tokens_content);
          if (tokens_content != "") {
            result += separator;
            result += tokens_content;
            separator = ", ";
          }
        }
      }
      result += "]";
      std::cout << result << std::endl;
    } 
    
    // format the file content ...
    else {
      for (; file_path != end; file_path.increment(err)) {
        if (err) {
          std::cout << "WARNING: " << err << std::endl;
        }
        rc |= format(file_path->path());
      }
    }
  }
  return rc;
}
