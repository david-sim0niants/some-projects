#include "assembler.h"

#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <optional>


struct AssemblyParseState
{
    uint32_t next_instr_addr = 0;
    std::unordered_map<std::string, int32_t> &labels;
    std::remove_reference<decltype(labels)>::type::iterator last_defined_label_it = {}; // zakayf
    std::vector<std::string> &messages;
};


class AssemblyDef
{
public:
    char immediate_sign = '#';
    char delimiter = ',';
    const char *comment = "//";
    std::unordered_map<std::string, Mnemonic> mnemonics = {
        {"ADD", Mnemonic::ADD}, {"add", Mnemonic::ADD},
        {"SUB", Mnemonic::SUB}, {"sub", Mnemonic::SUB},
        {"OR",  Mnemonic::OR }, {"or",  Mnemonic::OR },
        {"NOT", Mnemonic::NOT}, {"not", Mnemonic::NOT},
        {"AND", Mnemonic::AND}, {"and", Mnemonic::AND},
        {"XOR", Mnemonic::XOR}, {"xor", Mnemonic::XOR},
        {"MUL", Mnemonic::MUL}, {"mul", Mnemonic::MUL},
        {"JE",  Mnemonic::JE }, {"je",  Mnemonic::JE },
        {"JNE", Mnemonic::JNE}, {"jne", Mnemonic::JNE},
        {"JLT", Mnemonic::JLT}, {"jlt", Mnemonic::JLT},
        {"JLE", Mnemonic::JLE}, {"jle", Mnemonic::JLE},
        {"JGT", Mnemonic::JGT}, {"jgt", Mnemonic::JGT},
        {"JGE", Mnemonic::JGE}, {"jge", Mnemonic::JGE},
        {"JMP", Mnemonic::JMP}, {"jmp", Mnemonic::JMP},
        {"MOV", Mnemonic::MOV}, {"mov", Mnemonic::MOV},
    };
    std::unordered_map<std::string, size_t> registers;

    static const AssemblyDef instance;
    static constexpr unsigned int MAX_MNM_LEN = 3;
    static constexpr unsigned int MAX_REG_LEN = 3;

private:
    AssemblyDef()
    {
        for (unsigned int i = 0; i < NUM_REGISTERS; ++i)
        {
            std::string i_str = std::to_string(i);
            registers['r' + i_str] = i;
            registers['R' + i_str] = i;
        }

        registers["io"] = IO_REGISTER_INDEX;
        registers["IO"] = IO_REGISTER_INDEX;

        registers["pc"] = COUNTER_INDEX;
        registers["PC"] = COUNTER_INDEX;
    }
};


inline const AssemblyDef AssemblyDef::instance;


template<typename... Types>
bool is_in_Monostate(std::variant<Types...> variant)
{
    return std::holds_alternative<std::monostate>(variant);
}


static std::optional<OperandImmediate> parse_Immediate(std::istream &input)
{
    auto initial_pos = input.tellg();

    OperandImmediate immediate;

    while (input)
    {
        char c;
        input >> c;

        if (std::iswspace(c))
        {
            continue;
        }

        if (c != AssemblyDef::instance.immediate_sign)
        {
            input.seekg(initial_pos, std::ios::beg);
            return {};
        }

        break;
    }

    if (!input)
    {
        input.seekg(initial_pos, std::ios::beg);
        return {};
    }

    input >> immediate;

    if (input.fail())
    {
        input.seekg(initial_pos, std::ios::beg);
        return {};
    }

    if (input)
    {
        char c;
        input >> c;

        if (!std::iswspace(c) && c != AssemblyDef::instance.delimiter)
        {
            input.seekg(initial_pos, std::ios::beg);
            return {};
        }
    }

    return immediate;
}


static std::string parse_Identifier(std::istream &input)
{
    int chars_read = 0;
    std::string identifier;

    while (input)
    {
        char c;
        input >> c;

        if (identifier.empty())
        {
            if (std::iswspace(c))
            {
                continue;
            }

            if ((c < 'A' || c > 'Z') && (c < 'a' || c > 'z') && c != '.' && c != '_')
            {
                break;
            }

            identifier.push_back(c);
        }
        else
        {
            if (std::iswspace(c) ||
                (c < 'A' || c > 'Z') && (c < 'a' || c > 'z') &&
                (c < '0' || c > '9') && c != '.' && c != '_')
            {
                break;
            }

            identifier.push_back(c);
        }

    }

    if (identifier.empty())
    {
        input.seekg(-chars_read - 1, std::ios::cur);
    }
    else
    {
        input.seekg(-1, std::ios::cur);
    }

    return identifier;
}


static bool check_if_Reserved(std::string &identifier)
{
    return !
    (AssemblyDef::instance.mnemonics.find(identifier) == AssemblyDef::instance.mnemonics.end()
    &&
    AssemblyDef::instance.registers.find(identifier) == AssemblyDef::instance.registers.end());
}


static bool parse_LabelDef(std::istream &input, AssemblyParseState &parse_state)
{
    auto initial_pos = input.tellg();
    parse_state.last_defined_label_it = {};

    std::string label = parse_Identifier(input);

    if (label.empty())
    {
        input.seekg(initial_pos, std::ios::beg);
        return false;
    }

    int chars_read = 0;
    while (input)
    {
        char c;
        input >> c;

        if (std::iswspace(c))
        {
            chars_read = 0;
            continue;
        }

        if (c != ':')
        {
            break;
        }

        if (parse_state.labels.find(label) != parse_state.labels.end())
        {
            parse_state.messages.push_back("Label already defined.");
            return false;
        }

        if (check_if_Reserved(label))
        {
            parse_state.messages.push_back("Token " + label + " is reserved and can't be a label name.");
            return false;
        }

        parse_state.last_defined_label_it = parse_state.labels.insert(
            std::make_pair(label, parse_state.next_instr_addr)
        ).first;
        return true;
    }

    input.seekg(initial_pos, std::ios::beg);
    return false;
}


static Mnemonic parse_Mnemonic(std::istream &input)
{
    auto initial_pos = input.tellg();

    std::string token;
    input.setf(std::ios::skipws);
    input >> token;
    input.unsetf(std::ios::skipws);

    auto &mnemonics = AssemblyDef::instance.mnemonics;

    auto found_it = mnemonics.find(token);
    if (found_it != mnemonics.end())
    {
        return found_it->second;
    }
    else
    {
        input.seekg(initial_pos, std::ios::beg);
        return Mnemonic::NONE;
    }
}


static OperandMemLoc parse_Register(std::istream &input)
{
    auto initial_pos = input.tellg();

    char reg_name[AssemblyDef::MAX_REG_LEN + 1]{};
    unsigned int i = 0;

    char c;

    while (input && i < AssemblyDef::MAX_REG_LEN)
    {
        input >> c;

        if (std::iswspace(c) || c == AssemblyDef::instance.delimiter)
        {
            if (i)
                break;
            else
                continue;
        }

        reg_name[i++] = c;
    }

    if (!input && !(std::iswspace(c) || c == AssemblyDef::instance.delimiter))
    {
        return NUM_REGISTERS;
    }

    auto found_it = AssemblyDef::instance.registers.find(std::string{reg_name});

    if (found_it == AssemblyDef::instance.registers.end())
    {
        input.seekg(initial_pos, std::ios::beg);
        return NUM_REGISTERS;
    }

    return found_it->second;
}


static OperandMemLoc parse_Address(std::istream &input)
{
    auto initial_pos = input.tellg();

    while (input)
    {
        char c;
        input >> c;

        if (!(std::iswspace(c) || c == AssemblyDef::instance.delimiter))
        {
            input.seekg(-1, std::ios::cur);
            break;
        }
    }

    OperandMemLoc addr;
    input >> addr;

    if (input.fail())
    {
        input.seekg(initial_pos, std::ios::beg);
        return 0;
    }

    if (input)
    {
        char c;
        input >> c;

        if (!(std::iswspace(c) || c == AssemblyDef::instance.delimiter))
        {
            input.seekg(initial_pos, std::ios::beg);
            return 0;
        }
    }

    return addr;
}


static std::optional<OperandMemLoc> parse_MemoryLocation(std::istream &input)
{
    OperandMemLoc mem_loc = parse_Register(input);

    if (mem_loc < NUM_REGISTERS)
    {
        return mem_loc;
    }

    // mem_loc = parse_Address(input);
    // if (mem_loc >= NUM_REGISTERS)
    // {
    //     return mem_loc;
    // }

    return {};
}


static SrcOperand parse_SrcOperand(std::istream &input, AssemblyParseState &parse_state)
{
    auto mem_loc = parse_MemoryLocation(input);
    if (mem_loc.has_value())
    {
        return mem_loc.value();
    }

    auto immediate = parse_Immediate(input);
    if (immediate.has_value())
    {
        return immediate.value();
    }

    std::string label = parse_Identifier(input);

    if (check_if_Reserved(label))
    {
        parse_state.messages.push_back("Invalid label - " + label);
        return {};
    }

    if (!label.empty())
    {
        return label;
    }

    return std::monostate{};
}


static DstOperand parse_DstOperand(std::istream &input, AssemblyParseState &parse_state)
{
    auto mem_loc = parse_MemoryLocation(input);
    if (mem_loc.has_value())
    {
        return mem_loc.value();
    }

    std::string label = parse_Identifier(input);
    if (!label.empty())
    {
        return label;
    }

    if (check_if_Reserved(label))
    {
        parse_state.messages.push_back("Invalid label - " + label);
        return {};
    }

    return std::monostate{};
}


static std::optional<Instruction> parse_Instruction(std::istream &input, AssemblyParseState &parse_state)
{
    Mnemonic mnemonic = parse_Mnemonic(input);

    const char *src_expected = "Expected a source operand.";
    const char *dst_expected = "Expected a destination operand.";

    if (mnemonic == Mnemonic::NONE)
    {
        return {};
    }

    if (mnemonic == Mnemonic::JMP)
    {
        DstOperand dst = parse_DstOperand(input, parse_state);
        if (is_in_Monostate(dst))
        {
            parse_state.messages.push_back(dst_expected);
            return {};
        }
        return Instruction{.mnemonic = mnemonic, .dst = dst};
    }

    SrcOperand src1 = parse_SrcOperand(input, parse_state);
    if (is_in_Monostate(src1))
    {
        parse_state.messages.push_back(src_expected);
        return {};
    }

    if (mnemonic == Mnemonic::MOV)
    {
        DstOperand dst = parse_DstOperand(input, parse_state);
        if (is_in_Monostate(dst))
        {
            parse_state.messages.push_back(dst_expected);
            return {};
        }
        return Instruction{.mnemonic = mnemonic, .src1 = src1, .dst = dst};
    }

    SrcOperand src2 = parse_SrcOperand(input, parse_state);
    if (is_in_Monostate(src2))
    {
        parse_state.messages.push_back(src_expected);
        return {};
    }

    DstOperand dst = parse_DstOperand(input, parse_state);
    if (is_in_Monostate(dst))
    {
        parse_state.messages.push_back(dst_expected);
        return {};
    }

    return Instruction{mnemonic, src1, src2, dst};
}


void parse_Assembly(std::istream &input, Assembly &assembly, std::vector<std::string> &messages)
{
    AssemblyParseState parse_state {
        .labels = assembly.labels,
        .messages = messages
    };

    input.unsetf(std::ios::skipws);

    while (input)
    {
        if (parse_LabelDef(input, parse_state))
        {
            // check if the label is just a constant actually
            // constant's syntax is the same as immediate's one
            auto immediate = parse_Immediate(input);
            if (!immediate.has_value())
            {
                continue;
            }

            parse_state.last_defined_label_it->second = immediate.value();
            continue;
        }

        auto instruction = parse_Instruction(input, parse_state);
        if (instruction.has_value())
        {
            assembly.instructions.push_back(instruction.value());
            parse_state.next_instr_addr += instruction.value().size();
            continue;
        }

        input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
}


void preprocess(std::istream &input, std::ostringstream &output)
{
    while (input)
    {
        std::string line;
        std::getline(input, line);
        size_t found_comment_pos = line.find(AssemblyDef::instance.comment);

        if (found_comment_pos != std::string::npos)
        {
            output << line.substr(0, found_comment_pos);
        }
        else
        {
            output << line;
        }

        output << std::endl;
    }
}


void assemble(const Assembly &assembly, std::ostream &output, std::vector<std::string> &messages)
{

}


void assemble(std::istream &input, std::ostream &output, std::vector<std::string> &messages)
{
    std::ostringstream preprocessed_output;
    preprocess(input, preprocessed_output);

    std::istringstream clean_input(preprocessed_output.str());
    Assembly assembly;
    parse_Assembly(clean_input, assembly, messages);

    assemble(assembly, output, messages);
}

