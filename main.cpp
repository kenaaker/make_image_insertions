#include <iostream>
#include <png.h>
#include <zlib.h>
#include <errno.h>
#include <getopt.h>
#include <list>
#include <regex.h>
#include <algorithm>
#include <sstream>
#include <Magick++.h>

using namespace std;
using namespace Magick;

struct _geom_angle {
    Geometry geom;              /* geometry location WxH+x+y */
    double rotation_degrees;    /* rotation of insertion image before insert */
    bool operator<(const _geom_angle& rhs) const {
        if (geom < rhs.geom) {
            return true;
        } else if (geom > rhs.geom) {
            return false;
        } else {
            return (rotation_degrees < rhs.rotation_degrees);
        } /* endif */
    } /* operator< */
    bool operator==(const _geom_angle& rhs) const {
        return ((geom == rhs.geom) &&
                (rotation_degrees == rhs.rotation_degrees));
    } /* operator== */
};

static void make_insertions_version_info() {
    cout << "   Compiled with libpng " << PNG_LIBPNG_VER_STRING <<
            " using libpng " << png_libpng_ver << "." << endl;
    cout << "   Compiled with zlib " << ZLIB_VERSION <<
            " using zlib " << zlib_version << "." << endl;
} /* make_insertions_version_info */

static void usage(void) {
    make_insertions_version_info();
    cout << " Usage is make_insertions -w geometry_spec -w geometry_spec... "
            "insert_image_file target_image_file output_image_file" << endl;
} /* usage */

/* Put insertions into target image. inserts is assumed sorted and unique */
static int process_image(Image &out_img, Image &insert_img, Image &target_img,
                        list<struct _geom_angle> inserts) {
    int rc =-1;
    out_img = target_img;
    cout << "Starting to process image" << endl;
    out_img = target_img;
    list<struct _geom_angle>::iterator ci;
    for (ci=inserts.begin(); ci!=inserts.end(); ++ci) {
        /* Take the current insert geometry item, size the logo image and insert it into the target */
        Image inset = insert_img;
        inset.backgroundColor(insert_img.backgroundColor());
        inset.rotate(ci->rotation_degrees);
        inset.zoom(ci->geom);
        /* Calculate the center of the insert area and the center of the inserted image */
        /* Align the two centers */
        int inset_img_width = inset.columns();
        int inset_img_height = inset.rows();
        int geom_width = ci->geom.width();
        int geom_height = ci->geom.height();
        int center_x_offset = geom_width/2 - inset_img_width/2;
        int center_y_offset = geom_height/2 - inset_img_height/2;
        ci->geom.xOff(ci->geom.xOff() + center_x_offset);
        ci->geom.yOff(ci->geom.yOff() + center_y_offset);
        inset.transparent(insert_img.backgroundColor());
        out_img.composite(inset,ci->geom,OverCompositeOp);
        rc = 0;
    } /* endfor */
    cout << "Finished processing image" << endl;
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
        if (text[i].key == string("insert_loc")) {
            png_inserts.push_back(text[i].text);
        } /* endif */
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

static list<string> get_infile_inserts(string in_file) { /* Just for png files now */
    png_struct *png;
    png_info_struct *png_info; /* png info for chunks before the image */
    png_info_struct *end_info; /* png info for chunks after the image */
    FILE *fp;
    list<string> infile_insertions;

    if ((fp= fopen(in_file.c_str(), "rb")) != NULL) {
        int bytes_read;
        if (is_png_file(fp, &bytes_read)) {
            png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
            if (png != NULL) {
                png_info = png_create_info_struct(png);
                if (png_info != NULL) {
                    end_info = png_create_info_struct(png);
                    if (end_info != NULL) {
                        png_init_io(png, fp);
                        png_read_png(png, png_info, PNG_TRANSFORM_IDENTITY, png_voidp_NULL);
                        infile_insertions = get_png_insertion_texts(png, png_info);
                        png_destroy_info_struct(png, &end_info);
                    } /* endif */
                    png_destroy_info_struct(png, &png_info);
                } /* endif */
                png_destroy_read_struct(&png, png_infopp_NULL, png_infopp_NULL);
            } /* endif */
        } /* endif */
        fclose(fp);
    } /* endif */
    return infile_insertions;
} /* get_infile_inserts */

static int display_infile_inserts(string in_file) { /* Just for png files now */
    int rc = 0;

    list<string> infile_insertions = get_infile_inserts(in_file);
    if (infile_insertions.empty()) {
        rc = -EIO;
    } else {
        for (list<string>::iterator it=infile_insertions.begin(); it!=infile_insertions.end(); ++it) {
            cout << " insert location = \"" << *it << "\"" << endl;
        } /* endfor */
    } /* endif */
    return rc;
} /* display_infile_inserts */

static inline int string_to_int(string s)
{
    stringstream ss(s);
    int x;
    ss >> x;
    return x;
}

static const struct _geom_angle ins_loc_to_geom(string ins_str) {
    string geom_str;
    string rotate_str;
    /* split the string at the optional '/' to isolate the geometry and rotation */
    string rotate_delim = "/";
    int delim_pos = ins_str.find(rotate_delim);
    double rotate_angle = 0;

    if (delim_pos > 0) {
        geom_str = ins_str.substr(0,delim_pos);
        rotate_str = ins_str.erase(0,delim_pos+rotate_delim.length());
        rotate_angle = string_to_int(rotate_str);
    } else {
        geom_str = ins_str;
    } /* endif */
    Geometry ins_geom;
    try {
        ins_geom = Geometry(geom_str);
    } catch (Exception &error_){
        cout << "Failed to create an insertion geometry from \"" << geom_str << endl;
        cout << " Error is " << error_.what() << endl;
        ins_geom = Geometry(0,0,0,0);
    } /* catch */
    struct _geom_angle const new_insert_spec = { ins_geom, rotate_angle };
    return new_insert_spec;
} /* ins_loc_to_geom */

/* This program takes 3 filename arguments and multiple -w insertion spec strings */
/* Positional arguments are inserted_image_file, target_image_file, output_image_file */
int main(int argc, char* argv[]) {

    int rc = 0;
    int opt = 0;
    list<struct _geom_angle> insert_geoms;
    list<string> cmd_insert_strs;
    list<string> template_inserts;
    enum process_options {display_insertions, do_insertions, do_nothing};
    enum process_options p_opt = do_insertions;
    string insert_img_filename;
    string target_img_filename;
    string output_img_filename;
    Image insert_img;
    Image target_img;
    Image output_img;

    static struct option long_options[] = {
        { "display-insert-locs", required_argument, 0, 'd'},
        { "insert-spec", required_argument, 0, 'w'},
        { "insert-img", required_argument, 0, 'i'},
        { "target-img", required_argument, 0, 't'},
        { "output-img", required_argument, 0, 'o'},
        { 0, 0, 0, 0 },
    }; /* long_options */
    int option_index = 1;

    InitializeMagick(*argv);

    rc = 0;
    while ((rc == 0) &&
           (opt = getopt_long(argc, argv, "dw:i:t:o:", long_options, &option_index)) != -1) {
        list<struct _geom_angle>::iterator ci;
        switch(opt) {
            case 'd':
                p_opt= display_insertions;
                break;
            case 'w':
                /* add the option to the list of insertion points */
                cmd_insert_strs.push_back(optarg);
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

    if (((argc-optind) == 3) && (p_opt == do_insertions)) {
        rc = 0;
        insert_img_filename = argv[optind];
        target_img_filename = argv[optind+1];
        output_img_filename = argv[optind+2];
        if (!cmd_insert_strs.empty()) {
            template_inserts = cmd_insert_strs;
        } else {
            /* See if there are insert locations in the template file */
            cout << " Looking for insert items in target file " << endl;
            template_inserts = get_infile_inserts(target_img_filename);
        } /* endif */
        list<string>::iterator ti;
        cout << " list from target file is " << template_inserts.size() << endl;
        for (ti=template_inserts.begin(); ti!=template_inserts.end(); ++ti) {
            list<struct _geom_angle>::iterator ci;
            struct _geom_angle new_insert_spec = ins_loc_to_geom(*ti);
            if (new_insert_spec.geom == Geometry(0,0,0,0)) { /* width and height of 0 mean no insertion */
                cout << " This insert item from the target file \"" << *ti << "\" cannot be used for insertion, should be widthxheight+xoffset+yoffset/rotation_angle" << endl;
                p_opt = do_nothing;
                exit(-EINVAL);
            } else {
                ci = find(insert_geoms.begin(), insert_geoms.end(), new_insert_spec);
                if (ci != insert_geoms.end()) {
                    cout << " This insert item \"" << *ti << "\" is a duplicate, check the list of embedded inserts using the -d option." << endl;
                    p_opt = do_nothing;
                    exit(-EINVAL);
                } else {
                    insert_geoms.push_back(new_insert_spec);
                    p_opt = do_insertions;
                } /* endif */
            } /* endif */
        } /* endfor */
        insert_geoms.sort();
        insert_geoms.unique();
        try {
            insert_img.read(insert_img_filename);
        } catch(Magick::WarningCoder &warning_) {
            cout << "Warning is " << warning_.what() << endl;
            insert_img.colorSpace(sRGBColorspace);
        } catch(Exception &error_) {
            cout << "Error is " << error_.what() << endl;
            rc = -EIO;
        } /* try catch */

        if (rc == 0) {
            try {
                target_img.read(target_img_filename);
            } catch(Magick::WarningCoder &warning_) {
                cout << "Warning is " << warning_.what() << endl;
                target_img.colorSpace(sRGBColorspace);
            } catch(Exception &error_) {
                cout << "Exception is " << error_.what() << endl;
                rc = -EIO;
            } /* try catch */
        } /* endif */
        if (rc == 0) {
            rc = process_image(output_img, insert_img, target_img, insert_geoms);
            if (rc == 0) {
                try {
                    output_img.write(output_img_filename);
                } catch(Magick::WarningCoder &warning_) {
                    cout << "Output Warning is " << warning_.what() << endl;
                } catch(Exception &error_) {
                    cout << "Output Exception is " << error_.what() << endl;
                    rc = -EIO;
                } /* try catch */
            } /* endif */
        } /* endif */
    } else {
        if (((argc-optind) == 1) && (p_opt == display_insertions)) {
            rc = display_infile_inserts(argv[optind]);
        } else {
            usage();
            rc = 1;
        }
    } /* endif */
    return rc;
}
