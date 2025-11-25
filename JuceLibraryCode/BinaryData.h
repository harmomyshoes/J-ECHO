/* =========================================================================================

   This is an auto-generated file: Any edits you make may be overwritten!

*/

#pragma once

namespace BinaryData
{
    extern const char*   Brand_84px_png;
    const int            Brand_84px_pngSize = 1478;

    extern const char*   Brand_96px_png;
    const int            Brand_96px_pngSize = 2344;

    extern const char*   Brand_120px_png;
    const int            Brand_120px_pngSize = 4284;

    extern const char*   LOGO_With_Vase_PNG;
    const int            LOGO_With_Vase_PNGSize = 127981;

    extern const char*   Theme9_jpg;
    const int            Theme9_jpgSize = 3425;

    extern const char*   Brand_png;
    const int            Brand_pngSize = 18761;

    extern const char*   magic_xml;
    const int            magic_xmlSize = 4175;

    // Number of elements in the namedResourceList and originalFileNames arrays.
    const int namedResourceListSize = 7;

    // Points to the start of a list of resource names.
    extern const char* namedResourceList[];

    // Points to the start of a list of resource filenames.
    extern const char* originalFilenames[];

    // If you provide the name of one of the binary resource variables above, this function will
    // return the corresponding data and its size (or a null pointer if the name isn't found).
    const char* getNamedResource (const char* resourceNameUTF8, int& dataSizeInBytes);

    // If you provide the name of one of the binary resource variables above, this function will
    // return the corresponding original, non-mangled filename (or a null pointer if the name isn't found).
    const char* getNamedResourceOriginalFilename (const char* resourceNameUTF8);
}
