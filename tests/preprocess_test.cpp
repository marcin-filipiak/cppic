#include "preprocess/preprocessor.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static int fails = 0;
static int checks = 0;

static void check(bool cond, const char* what) {
    ++checks;
    if (cond) {
        std::printf("  ok  %s\n", what);
    } else {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++fails;
    }
}


static std::string stripWs(const std::string& s) {
    std::string out;
    for (char c : s)
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') out += c;
    return out;
}

static void writeFile(const fs::path& p, const std::string& content) {
    std::ofstream f(p, std::ios::binary);
    f << content;
}

int main() {
    fs::path dir = fs::temp_directory_path() / "cppic_preprocess_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::create_directories(dir / "sub");

    // --- #define substitution --------------------------------------------
    {
        std::printf("case: #define substitution\n");
        fs::path f = dir / "define.cpp";
        writeFile(f, "#define LED_PIN 13\nvoid f() { PORTB |= (1 << LED_PIN); }\n");
        cppic::Preprocessor pp;
        auto r = pp.run({f.string()});
        check(stripWs(r.source).find("PORTB|=(1<<13)") != std::string::npos,
              "#define constant substituted");
        check(stripWs(r.source).find("LED_PIN") == std::string::npos,
              "macro name gone from output");
    }

    // --- object-like guard + #ifndef skip --------------------------------
    {
        std::printf("case: include guard\n");
        fs::path h = dir / "g.h";
        fs::path f = dir / "guard.cpp";
        writeFile(h, "#ifndef G_H\n#define G_H\nint guarded = 4;\n#endif\n");
        writeFile(f,
                  "#include \"g.h\"\n"
                  "#include \"g.h\"\n"
                  "void f() { int x = guarded; }\n");
        cppic::Preprocessor pp;
        pp.addIncludePath(dir.string());
        auto r = pp.run({f.string()});
        check(stripWs(r.source).find("intguarded=4;") != std::string::npos,
              "guarded content inlined");
        check(stripWs(r.source).find("G_H") == std::string::npos,
              "guard macros not leaking into code");
    }

    // --- #if/#elif/#else -------------------------------------------------
    {
        std::printf("case: conditionals\n");
        fs::path f = dir / "cond.cpp";
        writeFile(f,
                  "#define X 2\n"
                  "#if X == 2\nint two = 1;\n"
                  "#elif X == 3\nint three = 1;\n"
                  "#else\nint other = 1;\n"
                  "#endif\n"
                  "#if defined(MISSING)\nint missing = 1;\n#endif\n"
                  "#if !defined(MISSING) && X != 1\nint keep = 1;\n#endif\n");
        cppic::Preprocessor pp;
        auto r = pp.run({f.string()});
        check(stripWs(r.source).find("inttwo=1;") != std::string::npos, "if-branch active");
        check(stripWs(r.source).find("intthree=1;") == std::string::npos, "elif skipped");
        check(stripWs(r.source).find("intother=1;") == std::string::npos, "else skipped");
        check(stripWs(r.source).find("intmissing=1;") == std::string::npos,
              "defined() false branch skipped");
        check(stripWs(r.source).find("intkeep=1;") != std::string::npos,
              "!defined && x==2 kept");
    }

    // --- system include forwarding ---------------------------------------
    {
        std::printf("case: unresolved includes forwarded\n");
        fs::path f = dir / "sys.cpp";
        writeFile(f, "#include <Arduino.h>\nvoid f() {}\n");
        cppic::Preprocessor pp;
        auto r = pp.run({f.string()});
        check(stripWs(r.source).find("#include") == std::string::npos,
              "directive removed from source");
        bool found = false;
        for (const auto& s : r.systemIncludes) if (s == "Arduino.h") found = true;
        check(found, "Arduino.h forwarded as system include");
    }

    // --- runtime include suppressed (transpiler adds it) -----------------
    {
        std::printf("case: cppic_runtime.h not forwarded\n");
        fs::path f = dir / "runtime.cpp";
        writeFile(f, "#include \"cppic_runtime.h\"\nvoid f() {}\n");
        cppic::Preprocessor pp;
        auto r = pp.run({f.string()});
        check(r.systemIncludes.empty(), "cppic_runtime.h not forwarded");
    }

    // --- multi-file linking ----------------------------------------------
    {
        std::printf("case: multi-file link\n");
        fs::path a = dir / "a.cpp";
        fs::path b = dir / "b.cpp";
        writeFile(a, "int shared = 3;\n");
        writeFile(b, "void setup() { int y = shared; }\n");
        cppic::Preprocessor pp;
        auto r = pp.run({a.string(), b.string()});
        check(stripWs(r.source).find("shared=3;voidsetup") != std::string::npos,
              "both files merged in order");
    }

    // --- include cycle terminates ----------------------------------------
    {
        std::printf("case: include cycle\n");
        fs::path h = dir / "cyc.h";
        writeFile(h, "#include \"cyc.h\"\nint cyc = 1;\n");
        fs::path f = dir / "cyc.cpp";
        writeFile(f, "#include \"cyc.h\"\nvoid f() { int x = cyc; }\n");
        cppic::Preprocessor pp;
        auto r = pp.run({f.string()});
        check(stripWs(r.source).find("intcyc=1;") != std::string::npos,
              "cycle did not hang, content present");
    }

    // --- #error -----------------------------------------------------------
    {
        std::printf("case: #error\n");
        fs::path f = dir / "err.cpp";
        writeFile(f, "#if 1\n#error oops\n#endif\n");
        cppic::Preprocessor pp;
        bool threw = false;
        try { (void)pp.run({f.string()}); }
        catch (const cppic::PreprocessError& e) { threw = true; }
        check(threw, "#error raises PreprocessError");
    }

    // --- line continuation in #define ------------------------------------
    {
        std::printf("case: continuation\n");
        fs::path f = dir / "cont.cpp";
        writeFile(f, "#define BASE 4 \\\n 5\nvoid f() { int x = BASE; }\n");
        cppic::Preprocessor pp;
        auto r = pp.run({f.string()});
        check(stripWs(r.source).find("intx=45;") != std::string::npos,
              "backslash continuation joined");
    }

    std::printf("\npreprocess_test: %d/%d passed\n", checks - fails, checks);
    fs::remove_all(dir);
    return fails ? 1 : 0;
}