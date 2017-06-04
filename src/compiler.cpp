#include <unordered_map>
#include <iostream>
#include <vector>
#include <sstream>
#include <string>
#include <cstdint>
#include <algorithm>
#include <utility>
#include <stdexcept>
#include <functional>
#include <fstream>
#include <iomanip>
#include <queue>

namespace chip8
{
    namespace compiler
    {
        namespace
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

            std::uint16_t parse_insruction(std::string inst, const std::string& inst_params,
                    const std::unordered_map<std::string, unsigned>& labels)
            {
                std::cout << inst << ' ' << inst_params << std::endl;
                std::vector<std::string> opcode_args;
                {
                    std::istringstream sstream_params{inst_params};
                    std::string param;
                    unsigned regs = 0;
                    while (std::getline(sstream_params, param, ','))
                    {
                        auto opcode_arg_pattern = [&](const std::string& hex_str) {
                            std::string opcode_arg(3 - regs - hex_str.size(), '0');
                            inst.push_back(' ');
                            inst.append(3 - regs, 'N');
                            std::copy(hex_str.cbegin(), hex_str.cend(), std::back_inserter(opcode_arg));
                            opcode_args.push_back(std::move(opcode_arg));
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
                                    if (hex_str.size() > 3 - regs)
                                        return 0;
                                    opcode_arg_pattern(hex_str);
                                }
                                else
                                {
                                    const auto label_iter = labels.find(param);
                                    if (label_iter == labels.cend())
                                    {
                                        inst.push_back(' ');
                                        inst.append(param);
                                    }
                                    else
                                        opcode_arg_pattern(to_hex_string(label_iter->second));
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
                std::cout << inst << "  -->  " << final_opcode_str << std::endl;
                return std::stoul(final_opcode_str, nullptr, 16);
            }
        }

        std::vector<std::uint8_t> process(const std::string& source, unsigned mem_loc = 0x200)
        {
            std::vector<std::uint8_t> object_code;
            std::unordered_map<std::string/*identifier*/, unsigned/*address*/> labels;
            std::queue<std::pair<std::string/*instruction*/, std::string/*parameters*/>> instructions;
            {
                // parse the source code, find labels and instructions
                std::istringstream sstream_source{source};
                std::string line;
                while (std::getline(sstream_source, line))
                {
                    std::istringstream sstream_line{line};
                    std::string first_word;
                    if (sstream_line >> first_word) // is there anything?
                    {
                        auto add_instruction = [&instructions](std::istringstream& sstream, std::string inst) {
                            std::string params;
                            std::getline(sstream, params);
                            params.erase(std::remove(params.begin(), params.end(), ' '), params.end());
                            instructions.emplace(std::move(inst), std::move(params));
                        };
                        if (first_word.back() == ':' && first_word.size() > 1) // is it a label?
                        {
                            auto add_label = [&](std::string identifier) {
                                const auto result_pair = 
                                    labels.emplace(std::move(identifier), mem_loc + instructions.size() * 2);
                                if (!result_pair.second)
                                    std::cerr << "error: there is already a label: " << identifier << std::endl;
                            };
                            auto add_pattern = [&](std::istringstream& sstream, std::string label, std::string inst) {
                                label.pop_back(); // remove ':' character
                                add_label(std::move(label));
                                add_instruction(sstream, std::move(inst));
                            };
                            std::string second_word;
                            if (sstream_line >> second_word) // is there anything after the label?
                            {
                                if (second_word == "byte") // does the label refer to byte data?
                                {
                                    // ...
                                }
                                else // maybe the label refers to an instruction
                                {
                                    add_pattern(sstream_line, std::move(first_word), std::move(second_word));
                                }
                            }
                            else // find an instruction the label refers to
                            {
                                for (std::string new_line; std::getline(sstream_source, new_line);)
                                {
                                    std::istringstream sstream_new_line{new_line};
                                    std::string first_new_line_word; // instruction
                                    if (sstream_new_line >> first_new_line_word)
                                    {
                                        add_pattern(sstream_new_line, std::move(first_word), std::move(first_new_line_word));
                                        break;
                                    }
                                }
                            }
                        }
                        else // maybe it's an instruction
                        {
                            add_instruction(sstream_line, std::move(first_word));
                        }
                    }
                }
            }
            // run through instructions
            while (!instructions.empty())
            {
                const std::pair<std::string/*instruction*/, std::string/*parameters*/>& inst = instructions.front();
                const std::uint16_t opcode = parse_insruction(std::move(inst.first), inst.second, labels);
                if (!opcode)
                    std::cerr << "instruction parsing error" << std::endl;
                object_code.push_back(opcode >> 8 & 0xFF);
                object_code.push_back(opcode      & 0xFF);
                instructions.pop();
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
        std::ifstream stream{filepath, std::ios::in};
        if (!stream)
            throw std::runtime_error{"there is no a such file: " + filepath};
        return {std::istreambuf_iterator<char>{stream},
                std::istreambuf_iterator<char>{      }};
    }
}

int main()
{
    try
    {
        ::write_binary_file("res/chip8_bin/chip8_program.bin",
                chip8::compiler::process(::read_text_file("res/chip8_src/chip8_program.src")));
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
    }
    return 0;
}

