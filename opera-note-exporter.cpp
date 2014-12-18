
// Usage example:
// g++ opera-note-exporter.cpp -lboost_program_options -oopera-note-exporter -Wall -Wextra
// mkdir exported
// ./opera-note-exporter --tags-to-notebooks=1 notes.adr exported

#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/c_local_time_adjustor.hpp>
#include <boost/date_time/local_time_adjustor.hpp>

namespace po = boost::program_options;

typedef struct Options_t
{
    std::string default_tag;
    bool export_trash;
    std::string input;
    std::string output;
    bool convert_tags_to_notebooks;
} Options;

std::string EscapeXml(const std::string &str)
{
    // Thanks to Giovanni Funchal:
    // http://stackoverflow.com/a/5665377/1080482
    std::string buffer;
    buffer.reserve(str.size());
    for(size_t pos = 0; pos != str.size(); ++pos) {
        switch(str[pos]) {
        case '&':
            buffer.append("&amp;");
            break;
        case '\"':
            buffer.append("&quot;");
            break;
        case '\'':
            buffer.append("&apos;");
            break;
        case '<':
            buffer.append("&lt;");
            break;
        case '>':
            buffer.append("&gt;");
            break;
        default:
            buffer.append(&str[pos], 1);
            break;
        }
    }
    return buffer;
}

class Note
{
 public:
    Note() {}

    void SetTitle(const std::string &title)
    {
        title_ = title;
    }

    void SetNote(const std::string &note)
    {
        note_ = note;
    }

    void AddTag(const std::string &tag)
    {
        tags_.push_back(tag);
    }

    void SetUid(const std::string &uid)
    {
        uid_ = uid;
    }

    void SetCreationTimeFromUnixTime(const boost::uint32_t time)
    {
        typedef boost::date_time::c_local_adjustor<boost::posix_time::ptime> local_adj;
        creation_time_ = local_adj::utc_to_local(boost::posix_time::from_time_t((time_t)time));
    }

    std::string GetTomboyDateString() const
    {
        boost::posix_time::time_facet *facet(new boost::posix_time::time_facet);
        facet->format("%Y-%m-%dT%H:%M:%S.%f");
        std::stringstream ss;
        ss.imbue(std::locale(std::locale::classic(), facet));
        ss << creation_time_;
        return ss.str();
    }

    std::string AsTomboyNote(const Options &options) const
    {
        std::string datestr = GetTomboyDateString();
        std::string str = "<note version=\"0.3\" xmlns:link=\"http://beatniksoftware.com/tomboy/link\" xmlns:size=\"http://beatniksoftware.com/tomboy/size\" xmlns=\"http://beatniksoftware.com/tomboy\">\n";
        str += "\t<title>" + EscapeXml(title_) + "</title>\n";
        str += "\t<text xml:space=\"preserve\"><note-content version=\"0.1\">" + EscapeXml(note_) + "</note-content></text>\n";
        str += "\t<last-change-date>" + datestr + "</last-change-date>\n";
        str += "\t<last-metadata-change-date>" + datestr + "</last-metadata-change-date>\n";
        str += "\t<create-date>" + datestr + "</create-date>\n";
        str += "\t<tags>\n";
        for (size_t i = 0; i < tags_.size(); ++i) {
            str += "\t\t<tag>";
            if (options.convert_tags_to_notebooks) {
                str += "system:notebook:";
            }
            str += EscapeXml(tags_[i]) + "</tag>\n";
        }
        str += "\t</tags>\n";
        str += "\t<cursor-position>0</cursor-position>\n";
        str += "\t<width>450</width>\n";
        str += "\t<height>360</height>\n";
        str += "\t<x>0</x>\n";
        str += "\t<y>0</y>\n";
        str += "\t<open-on-startup>False</open-on-startup>\n";
        str += "</note>\n";
        return str;
    }

    void CreateTomboyNote(const Options &options) const
    {
        if (note_.size() == 0) {
            std::cout << "Skipping empty note" << std::endl;
            return;
        }

        if (uid_.size() == 0) {
            std::cerr << "Failed to create note: Note does not have UID" << std::endl;
        }
        std::ofstream file;
        const std::string out_fname(options.output + "/" + uid_ + ".note");
        file.open(out_fname.c_str());
        if (!file.is_open()) {
            std::cerr << "Failed to create note: Failed to create file" << std::endl;
            return;
        }
        file << AsTomboyNote(options) << std::endl;
        file.close();
        return;
    }
 private:
    std::string title_;
    std::string note_;
    boost::posix_time::ptime creation_time_;
    std::vector<std::string> tags_;
    std::string uid_;
}; //class Note

class OperaNoteParser
{
 public:
    OperaNoteParser(const Options &options)
        :
        options_(options)
    {
    }

    /// Parse input file
    void ParseFile()
    {
        std::ifstream file(options_.input.c_str());
        if (!file.is_open()) {
            std::cerr << "Failed to open input file" << std::endl;
            exit(-1);
        }

        std::string line;
        std::string current_folder_name;
        bool in_folder = false;
        bool in_trash = false;
        while (std::getline(file,line)) {
            const std::string prop = GetLinePropertyName(line);

            if (prop == "#FOLDER") {
                in_folder = true;
                in_trash = false;
                continue;
            } else if (prop == "TRASH FOLDER" && GetLinePropertyValue(line) == "YES") {
                in_trash = true;
            }

            if (in_trash && !options_.export_trash) {
                continue;
            }

            if (prop == "#NOTE") {
                // New note
                notes_.push_back(Note());
                in_folder = false;

                // Add default tag if it has been set
                if (options_.default_tag.size() > 0) {
notes_.rbegin()->AddTag(options_.default_tag);
                }
                if (current_folder_name.size() > 0) {
                    notes_.rbegin()->AddTag(current_folder_name);
                }
            } else if (!in_folder && prop == "UNIQUEID") {
                const std::string uid = GetLinePropertyValue(line);
                notes_.rbegin()->SetUid(uid);
            } else if (prop == "NAME") {
                const std::string name_str = GetLinePropertyValue(line);
                if (in_folder) {
                    current_folder_name = ParseFolderName(name_str);
                } else {
notes_.rbegin()->SetTitle(ParseNoteTitle(name_str));
notes_.rbegin()->SetNote(ParseNote(name_str));
                }
            } else if (prop == "CREATED") {
                const std::string date_str = GetLinePropertyValue(line);
                const boost::uint32_t date_val = boost::lexical_cast<boost::uint32_t>(date_str);
notes_.rbegin()->SetCreationTimeFromUnixTime(date_val);
            }
        }

        file.close();
    }

    /// Write parsed notes to output folder
    /// \param[in] options Input parameters for the program
    /// \param[in] notes Notes to be written
    void WriteFiles()
    {
        for (size_t i = 0; i < notes_.size(); ++i) {
            notes_[i].CreateTomboyNote(options_);
        }
    }
 private:

    /// Return first part of the string before <2>
    std::string ParseFolderName(const std::string &str)
    {
        typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
        std::string separators;
        separators += char(2);
        boost::char_separator<char> sep(separators.c_str());
        tokenizer tokens(str, sep);

        return *tokens.begin();
    }

    /// Parse note title from note line
    std::string ParseNoteTitle(const std::string &str)
    {
        typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
        std::string separators;
        separators += char(2);
        boost::char_separator<char> sep(separators.c_str());
        tokenizer tokens(str, sep);

        if (tokens.begin() != tokens.end()) {
            return *tokens.begin();
        } else {
            return "<no-title>";
        }
    }

    /// Parse note from note line
    std::string ParseNote(const std::string &str)
    {
        std::string separators;
        separators += char(2);
        separators += char(2);

        std::string replace_with;
        replace_with += '\n';

        std::string note = str;
        boost::replace_all(note,separators,replace_with);
        return note;
    }

    /// Get name of the property on the line
    std::string GetLinePropertyName(const std::string &line)
    {
        if (line.size() == 0) {
            return "";
        }

        size_t begin = 0;
        size_t len = 0;
        if (line[0] == '\t') {
            // Skip tab
            begin = 1;
        }

        while(line.size() > begin+len && line[begin+len] != '=') {
            ++len;
        }

        return line.substr(begin,len);
    }

    /// Get line property value
    std::string GetLinePropertyValue(const std::string &line)
    {
        size_t begin = 0;
        while(line.size() > begin && line[begin] != '=') {
            ++begin;
        }

        // Skip =
        ++begin;

        if (begin >= line.size()) {
            return "";
        }

        return line.substr(begin,line.size());
    }
 private:
    // Program options
    Options options_;

    // Parsed notes
    std::vector<Note> notes_;
}; //class OperaNoteParser

int main(int argc, char *argv[])
{
    Options options;
    options.export_trash = false;
    options.convert_tags_to_notebooks = false;

    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "Show help message")
        ("export-trash", po::value<bool>(&options.export_trash), "If true, also trash folder will be exported")
        ("tag", po::value<std::string>(&options.default_tag), "Tag which is added to all exports")
        ("input", po::value<std::string>(&options.input), "Input file")
        ("output", po::value<std::string>(&options.output), "Output file/folder")
        ("tags-to-notebooks", po::value<bool>(&options.convert_tags_to_notebooks), "If true, tags will be converted to notebooks")
    ;

    //Define command line parser positional options and parse
    po::positional_options_description p;
    p.add("input", 1);
    p.add("output", 1);
    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
    } catch(...) {
        std::cerr << "Failed top parse input" << std::endl;
        std::cerr << desc << std::endl;
        return -1;
    }
    po::notify(vm);

    //Do we need to print help?
    if (vm.count("help") || argc < 3) {
      std::cout << desc << std::endl;
      return 1;
    }

    OperaNoteParser parser(options);
    parser.ParseFile();
    parser.WriteFiles();

    return 0;
}

