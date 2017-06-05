#include <unordered_map>
#include <iterator>
#include <stdexcept>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <functional>
#include <queue>
#include <utility>
#include <cstdint>

namespace chip8
{
    namespace compiler
    {
        const std::unordered_map<std::string, std::string> instructions
        {
            {"cls",             "00E0"},
            {"ret",             "00EE"},
            {"jp NNN",          "1NNN"},
            {"call NNN",        "2NNN"},
            {"se vX NN",        "3XNN"},
            {"sne vX NN",       "4XNN"},
            {"se vX vY",        "5XY0"},
            {"ld vX NN",        "6XNN"},
            {"add vX N",        "7XNN"},
            {"ld vX vY",        "8XY0"},
            {"or vX vY",        "8XY1"},
            {"and vX vY",       "8XY2"},
            {"xor vX vY",       "8XY3"},
            {"add vX vY",       "8XY4"},
            {"sub vX vY",       "8XY5"},
            {"shr vX",          "8XY6"},
            {"subn vX vY",      "8XY7"},
            {"shl vX",          "8XYE"},
            {"sne vX vY",       "9XY0"},
            {"ld I NNN",        "ANNN"},
            {"jp v0 N",         "BNNN"},
            {"rnd vX N",        "CXNN"},
            {"drw vX vY N",     "DXYN"},
            {"skp vX",          "EX9E"},
            {"sknp vX",         "EXA1"},
            {"ld vX DT",        "FX07"},
            {"ld vX N",         "FX0A"},
            {"ld DT vX",        "FX15"},
            {"ld ST vX",        "FX18"},
            {"add I vX",        "FX1E"},
            {"ld F vX",         "FX29"},
            {"ld B vX",         "FX33"},
            {"ld [I] vX",       "FX55"},
            {"ld vX [I]",       "FX65"}
        };

        constexpr bool is_dec_digit(char c) noexcept {return c >= '0' && c <= '9';}

        constexpr bool is_hex_digit(char c) noexcept
        {
            return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
        }

        bool is_dec_number(const std::string& str) noexcept
        {
            return std::all_of(str.cbegin(), str.cend(), std::bind(is_dec_digit, std::placeholders::_1));
        }

        bool is_hex_number(const std::string& str) noexcept
        {
            return std::all_of(str.cbegin(), str.cend(), std::bind(is_hex_digit, std::placeholders::_1));
        }

        std::string to_hex_string(unsigned value)
        {
            std::ostringstream sstream_hex;
            sstream_hex << std::hex << value;
            return sstream_hex.str();
        }

        std::vector<std::uint8_t> parse_byte_data(std::istringstream& sstream)
        {
            std::vector<std::uint8_t> object_data;
            for (char c; sstream >> c;)
            {
                if (c == '"')
                {
                    while (sstream.get(c) && c != '"')
                        object_data.push_back(c);
                }
                else if (c != ',')
                {
                    if (!is_dec_digit(c))
                        return {};
                    else
                    {
                        std::string value{c};
                        while (sstream.get(c) && c != ',' && c != ' ')
                            value.push_back(c);
                        if (value[0] == '0' && value.size() > 2 && value[1] == 'x')
                            object_data.push_back(std::stoul(value, nullptr, 16));
                        else
                        {
                            if (!is_dec_number(value))
                                return {};
                            object_data.push_back(std::stoul(value, nullptr, 10));
                        }
                    }
                }
            }
            return object_data;
        }

        std::uint16_t parse_instruction(std::string inst, const std::string& inst_params,
                const std::unordered_map<std::string, unsigned>& labels)
        {
            std::cout << inst << ' ' << inst_params;
            std::vector<std::string> opcode_args;
            {
                std::istringstream sstream_params{inst_params};
                std::string param;
                unsigned regs = 0;
                while (std::getline(sstream_params, param, ','))
                {
                    auto opcode_arg_pattern = [&](const std::string& hex_str) -> bool {
                        if (hex_str.size() > 3 - regs)
                            return false;
                        std::string opcode_arg(3 - regs - hex_str.size(), '0');
                        inst.push_back(' ');
                        inst.append(3 - regs, 'N');
                        std::copy(hex_str.cbegin(), hex_str.cend(), std::back_inserter(opcode_arg));
                        opcode_args.push_back(std::move(opcode_arg));
                        return true;
                    };
                    switch (param[0])
                    {
                        case '[':
                            // ...
                            break;
                        case 'v':
                        {
                            if (param.size() != 2 || regs > 1 || !is_hex_digit(param[1]))
                                return 0;
                            opcode_args.push_back({param[1]});
                            inst.append(" v");
                            inst.push_back(!regs++ ? 'X' : 'Y');
                            break;
                        }
                        default:
                        {
                            if (is_dec_digit(param[0]))
                            {
                                std::string hex_str;
                                if (param[0] == '0' && param.size() > 2 && param[1] == 'x')
                                {
                                    if (!is_hex_number(hex_str = param.substr(2)))
                                        return 0;
                                }
                                else
                                {
                                    if (!is_dec_number(param))
                                        return 0;
                                    hex_str = to_hex_string(std::stoul(param, nullptr, 10));
                                }
                                if (!opcode_arg_pattern(hex_str))
                                    return 0;
                            }
                            else
                            {
                                const auto label_iter = labels.find(param);
                                if (label_iter == labels.cend())
                                {
                                    inst.push_back(' ');
                                    inst.append(param);
                                }
                                else if (!opcode_arg_pattern(to_hex_string(label_iter->second)))
                                    return 0;
                            }
                            break;
                        }
                    }
                }
            }
            const auto inst_iter = instructions.find(inst);
            if (inst_iter == instructions.cend())
                return 0;
            std::string final_opcode_str;
            for (unsigned opcode_arg_index = 0, i = 0; i < inst_iter->second.length(); ++i)
            {
                if (is_hex_digit(inst_iter->second[i]))
                    final_opcode_str.push_back(inst_iter->second[i]);
                else
                {
                    for (char c : opcode_args[opcode_arg_index])
                        final_opcode_str.push_back(c);
                    i += opcode_args[opcode_arg_index++].length() - 1;
                }
            }
            std::cout << "  -->  " << final_opcode_str << std::endl;
            return std::stoul(final_opcode_str, nullptr, 16);
        }

        std::vector<std::uint8_t> parse_compiled_line(const std::string& line,
                std::unordered_map<std::string, unsigned>& labels)
        {
            std::vector<std::uint8_t> bytes;
            std::istringstream sstream_line{line};
            std::string first_word;
            sstream_line >> first_word;
            if (first_word == "byte") // is it byte data?
            {
                const std::vector<std::uint8_t> byte_data{parse_byte_data(sstream_line)};
                if (byte_data.size())
                    std::copy(byte_data.cbegin(), byte_data.cend(), std::back_inserter(bytes));
            }
            else // maybe it's an instruction
            {
                std::string next_line_part; // parameters
                std::getline(sstream_line, next_line_part);
                next_line_part.erase(std::remove(next_line_part.begin(), next_line_part.end(), ' '), next_line_part.end());
                const std::uint16_t opcode = parse_instruction(std::move(first_word), next_line_part, labels);
                if (opcode)
                {
                    bytes.push_back(opcode >> 8 & 0xFF);
                    bytes.push_back(opcode      & 0xFF);
                }
            }
            return bytes;
        }

        unsigned compiled_line_size(const std::string& line)
        {
            std::istringstream sstream_line{line};
            std::string first_word;
            sstream_line >> first_word;
            if (first_word == "byte")
            {
                unsigned size = 0;
                for (char c; sstream_line >> c;)
                {
                    if (c == '"')
                    {
                        while (sstream_line.get(c) && c != '"')
                            ++size;
                    }
                    else if (c != ',')
                    {
                        if (!is_dec_digit(c))
                            return 0;
                        ++size;
                    }
                }
                std::cout << size << std::endl;
                return size;
            }
            else
                return 2;
        }

        std::vector<std::uint8_t> process(const std::string& source, unsigned PC = 0x200)
        {
            std::vector<std::uint8_t> object_code;
            std::unordered_map<std::string/*identifier*/, unsigned/*address*/> labels;
            std::queue<std::string> compiled_lines;
            {
                std::istringstream sstream_source{source};
                std::string line;
                while (std::getline(sstream_source, line))
                {
                    auto strip_comment = [](std::string& line) noexcept {
                        const auto cpos = line.find(';');
                        if (cpos != std::string::npos)
                            line.erase(cpos);
                    };
                    strip_comment(line);
                    std::istringstream sstream_line{line};
                    std::string first_word;
                    if (sstream_line >> first_word)
                    {
                        if (first_word.back() == ':' && first_word.size() > 1) // is it a label?
                        {
                            auto push_compiled_line = [&compiled_lines, &PC](std::string line) noexcept {
                                PC += compiled_line_size(line);
                                compiled_lines.push(std::move(line));
                            };
                            first_word.pop_back(); // remove ':' character
                            labels.emplace(std::move(first_word), PC);
                            std::string next_line_part;
                            if (std::getline(sstream_line, next_line_part)) // is there anything after the label?
                                push_compiled_line(std::move(next_line_part));
                            else
                            {
                                for (std::string new_line; std::getline(sstream_source, new_line);)
                                {
                                    // does the line contain non-whitespace symbols?
                                    if (new_line.find_first_not_of(' ') != std::string::npos)
                                    {
                                        strip_comment(new_line);
                                        push_compiled_line(std::move(new_line));
                                        break;
                                    }
                                }
                            }
                        }
                        else // maybe it's a compiled line
                        {
                            compiled_lines.push(std::move(line));
                            PC += 2;
                        }
                    }
                }
            }
            // compile all compiled lines
            for (; !compiled_lines.empty(); compiled_lines.pop())
            {
                const std::string& line = compiled_lines.front();
                const std::vector<std::uint8_t> bytes{parse_compiled_line(line, labels)};
                std::cout << line << std::endl;
                if (!bytes.size())
                    throw std::runtime_error{"error"};
                std::copy(bytes.cbegin(), bytes.cend(), std::back_inserter(object_code));
            }
            return object_code;
        }
    }
}

namespace
{
    void write_binary_file(const std::string& filepath, const std::vector<std::uint8_t>& data)
    {
        std::ofstream stream{filepath, std::ios::out | std::ios::binary};
        if (!stream)
            throw std::runtime_error{"there is no a such file: " + filepath};
        stream.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(std::uint8_t));
    }

    std::string read_text_file(const std::string& filepath)
    {
        std::ifstream stream{filepath};
        if (!stream)
            throw std::runtime_error{"there is no such a file " + filepath};
        return {std::istreambuf_iterator<char>{stream}, 
                std::istreambuf_iterator<char>{}};
    }
}

int main()
{
    try
    {
        write_binary_file("res/chip8_bin/chip8_program.bin",
                chip8::compiler::process(read_text_file("res/chip8_src/chip8_program.src")));
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
    }
    return 0;
}
