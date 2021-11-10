//
// Copyright 2019 Google LLC
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

#include "zetasql/public/sql_formatter.h"

#include <memory>
#include <vector>
#include <deque>
#include <string>

#include <iostream>
#include <algorithm>
#include <filesystem>


#include "zetasql/base/logging.h"
#include "zetasql/parser/parse_tree.h"
#include "zetasql/parser/parser.h"
#include "zetasql/public/error_helpers.h"
#include "zetasql/public/options.pb.h"
#include "zetasql/public/parse_location.h"
#include "zetasql/public/parse_resume_location.h"
#include "zetasql/public/parse_tokens.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "zetasql/base/source_location.h"
#include "zetasql/base/ret_check.h"
#include "zetasql/base/status.h"
#include "zetasql/base/status_builder.h"

namespace zetasql {


absl::Status FormatSql(const std::string& sql, std::string* formatted_sql) {
  ZETASQL_RET_CHECK_NE(formatted_sql, nullptr);
  formatted_sql->clear();

  *formatted_sql = sql;

  ParseTokenOptions options;
  options.include_comments = true;

  std::unique_ptr<ParserOutput> parser_output;

  ZETASQL_RETURN_IF_ERROR(ParseScript(sql, ParserOptions(),
                          ErrorMessageMode::ERROR_MESSAGE_MULTI_LINE_WITH_CARET, &parser_output));
  std::deque<std::pair<std::string, ParseLocationPoint>> comments;
  std::vector<ParseToken> parse_tokens;
  ParseResumeLocation location = ParseResumeLocation::FromStringView(sql);
  const absl::Status token_status =
      GetParseTokens(options, &location, &parse_tokens);
  if (token_status.ok()) {
    for (const auto& parse_token : parse_tokens) {
      if (parse_token.IsEndOfInput()) break;
      if (parse_token.IsComment()) {
        comments.push_back(std::make_pair(parse_token.GetSQL(), parse_token.GetLocationRange().start()));
      }
    }
    *formatted_sql = UnparseWithComments(parser_output->script(), comments);
  } else {
    // If GetParseTokens fails, just ignores comments.
    *formatted_sql = Unparse(parser_output->script());
  }

  return absl::OkStatus();
}


bool contains_char(std::string s, char ch) {
  bool found = false;
  const char* ss = s.c_str();
  size_t max_length = s.length();
  for (int i = 0; !found && i < max_length; i++) {
    found = ss[i] == ch;
  }
  return found;
}


std::string replace(std::string data, std::string to_search, std::string to_replace) {
  size_t pos = data.find(to_search);
  std::string ss = data;
  while(pos != std::string::npos) {
    ss.replace(pos, to_search.size(), to_replace);
    pos = ss.find(to_search, pos + to_replace.size());
  }
  return ss;
}

const std::string WHITESPACE = " \n\r\t\f\v";
 
std::string get_token_value(const ParseToken* parse_token) {
  std::string s = parse_token->GetSQL();
  size_t start = s.find_first_not_of(WHITESPACE);
  s = (start == std::string::npos) ? "" : s.substr(start);
  size_t end = s.find_last_not_of(WHITESPACE);
  s = (end == std::string::npos) ? "" : s.substr(0, end + 1);
  s = replace(s, "\r\n", "; "); // Windows new line
  s = replace(s, "\n", "; ");   // *nix new line
  std::string result = "";
  for (int i = 0; i < s.length(); i++) {
    if (s[i] == '"' && i > 0 && i < s.length() -1 && s[i-1] != '\\') {
      result.push_back('\\');
    }
    result.push_back(s[i]);
  }
  return result;
}


std::string get_token_item(const ParseToken* parse_token) {
  std::string token_type = "";
  if (parse_token->IsIdentifier()) token_type = "identifier";
  else if (parse_token->IsKeyword()) token_type = "keyword";
  else if (parse_token->IsValue()) token_type = "value";
  else if (parse_token->IsComment()) token_type = "comment";
  std::string token_value = get_token_value(parse_token);
  std::string result = "";
  if (token_type != "" && token_value != "") {
    if (token_type == "value") {
      result = "{\"type\": \"" + token_type + "\", \"value\": " + token_value + "}";
    } else {
      result = "{\"type\": \"" + token_type + "\", \"value\": \"" + token_value + "\"}";
    }
  }
  return result;
}


absl::Status ShowTokens(const std::filesystem::path& file_path, const std::string& tokens_filter, std::string* tokens_output) {

  ParseTokenOptions options;
  options.include_comments = true;

  std::unique_ptr<ParserOutput> parser_output;

  std::ifstream file(file_path, std::ios::in);
  std::string sql(std::istreambuf_iterator<char>(file), {});

  ZETASQL_RETURN_IF_ERROR(ParseScript(sql, ParserOptions(),
                          ErrorMessageMode::ERROR_MESSAGE_MULTI_LINE_WITH_CARET, &parser_output));

  std::deque<std::pair<std::string, ParseLocationPoint>> comments;
  std::vector<ParseToken> parse_tokens;

  ParseResumeLocation location = ParseResumeLocation::FromStringView(sql);
  const absl::Status token_status = GetParseTokens(options, &location, &parse_tokens);
  
  std::string tokens = "";
  std::string delimiter = "";
  if (token_status.ok()) {

    std::string filename = file_path;
    if (filename[0] != '\"') {
      filename = "\"" + filename + "\"";
    }

    tokens += "{ \"filename\": " + filename + ", \"tokens\": [";

    for (const auto& parse_token : parse_tokens) {

      if (
        (parse_token.IsIdentifier() && (contains_char(tokens_filter, 'i'))) ||
        (parse_token.IsKeyword() && (contains_char(tokens_filter, 'k'))) ||
        (parse_token.IsValue() && (contains_char(tokens_filter, 'v')))||
        (parse_token.IsComment() && (contains_char(tokens_filter, 'c'))) 
      ) {

        std::string token_item = get_token_item(&parse_token);
        if (token_item != "") {
          tokens += delimiter;
          tokens += token_item;
          delimiter = ", ";
        }
      }
      if (parse_token.IsEndOfInput()) break;
    }
    tokens += "]}";
  }
  *tokens_output = tokens;
  return absl::OkStatus();

}

}  // namespace zetasql
