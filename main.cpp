#include <iostream>
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

static inline int string_to_int(string s)
{
    stringstream ss(s);
    int x;
    ss >> x;
    return x;
}

static inline string int_to_string(int i)
{
    stringstream ss;
    ss << i;
    return ss.str();
}

// This is my override version of attribute to erase the previous value kda.
// Access/Update a named image attribute
void Magick::Image::attribute(const std::string name_,
                              const std::string value_) {
  modifyImage();
  if (value_.empty()) {
      SetImageAttribute(image(), name_.c_str(), NULL);
  } else {
      SetImageAttribute(image(), name_.c_str(), value_.c_str());
  } /* endif */
}


static void usage(void) {
    cout << " Usage is \tmake_image_insertions -w geometry_spec -w geometry_spec... "
            "insert_image_file template_image_file output_image_file" << endl;
    cout << " Or \t\tmake_image_insertions -w geometry_spec -w geometry_spec... "
            "-i insert_image_file -t template_image_file -o output_image_file" << endl;
    cout << " Or to display template file insertion points" << endl <<
            "\t\tmake_image_insertions -d template_image_file" << endl;
} /* usage */

/* Put insertions into target image. inserts is assumed sorted and unique */
static int process_image(Image &out_img, Image &insert_img, Image &target_img,
                        list<struct _geom_angle> inserts) {
    int rc =-1;
    string loc_keyword_string;
    string loc_string;
    int list_index = 1;
    out_img = target_img;
    cout << "Starting to process image" << endl;
    out_img = target_img;
    list<struct _geom_angle>::iterator ci;
    for (ci=inserts.begin(); ci!=inserts.end(); ++ci, ++list_index) {
        /* Take the current insert geometry item, size the logo image and insert it into the target */
        Image inset = insert_img;
        inset.backgroundColor(insert_img.backgroundColor());
        inset.rotate(ci->rotation_degrees);
        inset.zoom(ci->geom);
        /* Set the insert tags in the image attributes */
        loc_keyword_string = string("insert_loc_") + int_to_string(list_index);
        loc_string = int_to_string(ci->geom.width()) + "x" + int_to_string(ci->geom.height());
        loc_string += "+" + int_to_string(ci->geom.xOff()) + "+" + int_to_string(ci->geom.yOff());
        if (ci->rotation_degrees != 0) {
            loc_string += "/" + int_to_string(ci->rotation_degrees);
        } /* endif */
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
        if (!out_img.attribute(loc_keyword_string).empty()) {
            out_img.attribute(loc_keyword_string, ""); /* Delete old value */
        }
        out_img.attribute(loc_keyword_string, loc_string);
        rc = 0;
    } /* endfor */
    cout << "Finished processing image" << endl;
    return rc;
} /* process_image */

static list<string> get_infile_inserts(string in_file) {
    list<string> infile_insertions;
    Image attr_img;
    try {
        attr_img.read(in_file);
    } catch(Magick::WarningCoder &warning_) {
        cout << "Warning is " << warning_.what() << endl;
        attr_img.colorSpace(sRGBColorspace);
    } catch(Exception &error_) {
        cout << "Exception is " << error_.what() << endl;
    } /* try catch */

    int list_index = 1;
    string loc_keyword_string;
    string returned_loc_string;
    do {
        loc_keyword_string = string("insert_loc_") + int_to_string(list_index);
        returned_loc_string = attr_img.attribute(loc_keyword_string);
        if (!returned_loc_string.empty()) {
            infile_insertions.push_back(returned_loc_string);
        }
        ++list_index;
    } while(!returned_loc_string.empty());
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

    if ((((argc-optind) == 3) ||
         ((!insert_img_filename.empty() && !target_img_filename.empty() && !output_img_filename.empty())))
            && (p_opt == do_insertions)) {
        rc = 0;
        if (insert_img_filename.empty()) {
            insert_img_filename = argv[optind];
        } /* endif */
        if (target_img_filename.empty()) {
            target_img_filename = argv[optind+1];
        } /* endif */
        if (output_img_filename.empty()) {
            output_img_filename = argv[optind+2];
        } /* endif */
        if (!cmd_insert_strs.empty()) {
            template_inserts = cmd_insert_strs;
        } else {
            /* See if there are insert locations in the template file */
            template_inserts = get_infile_inserts(target_img_filename);
            if (template_inserts.empty()) {
                cout << "No image insertion points were supplied, stopping now." << endl;
                exit(-EINVAL);
            } else {
                cout << "Using insertion points from template file \"" << target_img_filename << "\"." << endl;
            }
        } /* endif */
        list<string>::iterator ti;
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
