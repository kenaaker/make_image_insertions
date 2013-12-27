#include <iostream>
#include <Magick++.h>
#include <png.h>
#include <zlib.h>
#include <errno.h>
#include <getopt.h>
#include <list>
#include <regex.h>
#include <algorithm>

using namespace std;
using namespace Magick;

static void make_insertions_version_info() {
    cout << "   Compiled with libpng " << PNG_LIBPNG_VER_STRING <<
            " using libpng " << png_libpng_ver << "." << endl;
    cout << "   Compiled with zlib " << ZLIB_VERSION <<
            " using zlib " << zlib_version << "." << endl;
} /* make_insertions_version_info */

static void usage(void) {
    make_insertions_version_info();
    cout << " Usage is make_insertions input_file output_file -w geometry_spec -w geometry_spec ..." << endl;
} /* usage */

/* Put insertions into target image. inserts is assumed sorted and unique */
static int process_image(Image &out_img, Image &insert_img, Image &target_img,
                        list<string> inserts) {
    int rc =-1;
    out_img = target_img;

    return rc;
} /* process_image */

/* Display insertion points in the input file */
static list<string> get_png_insertion_texts(png_struct *read_png,
                                            png_info_struct *read_png_info) {
    string text_key;
    string text_value;
    int text_chunks;
    int num_text;
    png_text *text;
    list<string> png_inserts;
    text_chunks = png_get_text(read_png, read_png_info, &text, &num_text);
    for (int i=0; i<text_chunks; ++i) {
        png_inserts.push_back(text[i].text);
    } /* endfor */
    return png_inserts;
} /* get_png_insertion_texts */

static bool is_png_file(FILE *fp, int *bytes_read) {
    unsigned char sig_bytes[8];
    int rc;
    if (fread(sig_bytes, 1, 8, fp) != 8) {
        rc = false;
    } else {
        if (png_sig_cmp(sig_bytes, 0, 8) == 0) {
            rc = true;
        } else {
            rc = false;
        } /* endif */
        fseek(fp, 0, SEEK_SET); /* put the file back at the beginning */
    } /* endif */
    return rc;
} /* is_png_file */

static int display_infile_inserts(string in_file) { /* Just for png files now */
    png_struct *png;
    png_info_struct *png_info; /* png info for chunks before the image */
    png_info_struct *end_info; /* png info for chunks after the image */
    FILE *fp;
    int rc =0;

    if ((fp= fopen(in_file.c_str(), "rb")) == NULL) {
        rc = -EIO;
    } else {
        int bytes_read;
        if (!is_png_file(fp, &bytes_read)) {
            rc = -EIO;
        } else {
            png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
            if (png == NULL) {
                rc = -ENOMEM;
            } else {
                png_info = png_create_info_struct(png);
                if (png_info == NULL) {
                    rc = -ENOMEM;
                } else {
                    end_info = png_create_info_struct(png);
                    if (end_info == NULL) {
                        rc = -ENOMEM;
                    } else {
                        png_init_io(png, fp);
                        png_read_png(png, png_info, PNG_TRANSFORM_IDENTITY, png_voidp_NULL);
                        list<string> infile_insertions = get_png_insertion_texts(png, png_info);
                        png_destroy_info_struct(png, &end_info);
                    } /* endif */
                    png_destroy_info_struct(png, &png_info);
                } /* endif */
                png_destroy_read_struct(&png, png_infopp_NULL, png_infopp_NULL);
            } /* endif */
        } /* endif */
        fclose(fp);
    } /* endif */
    return rc;
} /* process_png_inserts */


/* This program takes 3 filename arguments and multiple -w insertion spec strings */
/* Positional arguments are inserted_image_file, target_image_file, output_image_file */
int main(int argc, char* argv[]) {
    struct _geom_angle {
        Geometry geom;              /* geometry location WxH+x+y */
        double rotation_degrees;    /* rotation of insertion image before insert */
    };

    int rc = 0;
    int opt = 0;
    list<string> insert_list;
    list<struct _geom_angle> insert_geoms;
    enum process_options {display_insertions, do_insertions, do_nothing};
    enum process_options p_opt = do_nothing;
    string insert_img_filename;
    string target_img_filename;
    string output_img_filename;
    Image insert_img;
    Image target_img;
    Image output_img;

    static struct option long_options[] = {
        { "insert-spec", required_argument, 0, 'w'},
        { "insert-img", required_argument, 0, 'i'},
        { "target-img", required_argument, 0, 't'},
        { "output-img", required_argument, 0, 'o'},
        { 0, 0, 0, 0 },
    }; /* long_options */
    int option_index = 1;

    rc = 0;
    while ((rc == 0) &&
           (opt = getopt_long(argc, argv, "w:i:t:o:", long_options, &option_index)) != -1) {
        list<string>::iterator ci;
        switch(opt) {
            case 'w': {
                /* add the option to the list of insertion points */
                /* split the string at the optional '/' to isolate the geom and rotation */
                string cmd_ins_loc = string(optarg);
                string rotate_delim = "/";
                int delim_pos = cmd_ins_loc.find(rotate_delim);
                string geom_str = cmd_ins_loc.substr(0,delim_pos);
                string rotate_str = cmd_ins_loc.erase(0,delim_pos+rotate_delim.length());
                Geometry ins_geom;
                double rotate_angle = 0;
                try {
                    ins_geom = Geometry(geom_str);
                } catch (Exception &error_){
                    cout << "Failed to create an insertion geometry from \"" << geom_str << endl;
                    cout << " Error is " << error_.what() << endl;
                    rc = -EINVAL;
                } /* catch */
                struct _geom_angle new_insert_spec = { ins_geom, rotate_angle };
#ifdef foo
                ci = find(insert_list.begin(), insert_list.end(), new_insert_spec);
                if (ci != insert_list.end()) {
                    cout << " This insert item \"" << geom_str << "/" << rotate_str << "\" is a duplicate, check your list of -w arguments.." << endl;
                    p_opt = do_nothing;
                    exit(-EINVAL);
                }
#endif
                insert_list.push_back(string(optarg));
                p_opt = do_insertions;
                }
                break;
            case 'i':
                insert_img_filename = string(optarg);
                break;
            case 't':
                target_img_filename = string(optarg);
                break;
            case 'o':
                output_img_filename = string(optarg);
                break;
            default:
                usage();
                return -1;
        } /* endswitch */
    } /* endwhile */
    insert_list.sort();
    insert_list.unique();

    insert_img_filename = argv[optind];
    target_img_filename = argv[optind+1];
    output_img_filename = argv[optind+2];
    try {
        insert_img.read(insert_img_filename);
    } catch(Exception &error_) {
        cout << "Error trying to read input image named \"" << insert_img_filename << "\"" << endl;
        cout << "Error is " << error_.what() << endl;
    }

    try {
        target_img.read(target_img_filename);
    } catch(Exception &error_) {
        cout << "Error trying to read target image named \"" << target_img_filename << "\"" << endl;
        cout << "Error is " << error_.what() << endl;
    }

    if (((argc-optind) == 3) && (p_opt == do_insertions)) {
        rc =process_image(output_img, insert_img, target_img, insert_list);
    } else {
        if (((argc-optind) == 0) && (p_opt == display_insertions)) {
            rc = display_infile_inserts(argv[optind]);
        } else {
            usage();
            rc = 1;
        }
    } /* endif */
    return rc;
}
