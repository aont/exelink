#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{

    void usage(const char *program)
    {
        std::cerr << "usage: " << program << " INPUT SYMBOL OUTPUT\n";
    }

    bool is_valid_symbol(const std::string &symbol)
    {
        if (symbol.empty())
            return false;

        const auto first = static_cast<unsigned char>(symbol[0]);
        if (!(std::isalpha(first) || symbol[0] == '_'))
            return false;

        for (char ch : symbol)
        {
            const auto c = static_cast<unsigned char>(ch);
            if (!(std::isalnum(c) || ch == '_'))
                return false;
        }
        return true;
    }

    bool read_file(const std::string &path, std::vector<unsigned char> &data)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            std::cerr << "bin2c: cannot open input file: " << path << "\n";
            return false;
        }

        data.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        if (input.bad())
        {
            std::cerr << "bin2c: failed while reading input file: " << path << "\n";
            return false;
        }
        return true;
    }

    bool write_c_array(const std::string &path, const std::string &symbol, const std::vector<unsigned char> &data)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            std::cerr << "bin2c: cannot open output file: " << path << "\n";
            return false;
        }

        output << "static const unsigned char " << symbol << "[] = {\n";
        output << std::hex << std::setfill('0');
        for (std::size_t i = 0; i < data.size(); i += 12)
        {
            output << "    ";
            const std::size_t end = std::min<std::size_t>(i + 12, data.size());
            for (std::size_t j = i; j < end; ++j)
            {
                if (j != i)
                    output << ", ";
                output << "0x" << std::setw(2) << static_cast<unsigned int>(data[j]);
            }
            if (end < data.size())
                output << ',';
            output << '\n';
        }
        output << "};\n";
        output << std::dec << "static const unsigned int " << symbol << "_len = " << data.size() << ";\n";

        if (!output)
        {
            std::cerr << "bin2c: failed while writing output file: " << path << "\n";
            return false;
        }
        return true;
    }

} // namespace

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        usage(argv[0]);
        return 2;
    }

    const std::string input_path = argv[1];
    const std::string symbol = argv[2];
    const std::string output_path = argv[3];

    if (!is_valid_symbol(symbol))
    {
        std::cerr << "bin2c: invalid C symbol: " << symbol << "\n";
        return 2;
    }

    std::vector<unsigned char> data;
    if (!read_file(input_path, data))
        return 1;

    return write_c_array(output_path, symbol, data) ? 0 : 1;
}
