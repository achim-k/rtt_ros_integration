#!/usr/bin/python
import sys

import roslib
roslib.load_manifest("rtt_ros_integration");

import roscpp
import roscpp.msg_gen 
from  roslib import packages,msgs 
import os

from cStringIO import StringIO

NAME='create_boost_headers'

def write_boost_includes(s, spec):
    """
    Writes the message-specific includes
    
    @param s: The stream to write to
    @type s: stream
    @param spec: The message spec to iterate over
    @type spec: roslib.msgs.MsgSpec
    @param serializer: The serializer type for which to include headers
    @type serializer: str
    """
    for field in spec.parsed_fields():
        if (not field.is_builtin):
            if (field.is_header):
                s.write('#include "std_msgs/boost/Header.h"\n')
            else:
                (pkg, name) = roslib.names.package_resource_name(field.base_type)
                pkg = pkg or spec.package # convert '' to package
                s.write('#include "%s/boost/%s.h"\n'%(pkg,  name))
                
    s.write('\n') 


def write_boost_serialization(s, spec, cpp_name_prefix, file):
    """
    Writes the boost::serialize function for a message
    
    @param s: Stream to write to
    @type s: stream
    @param spec: The message spec
    @type spec: roslib.msgs.MsgSpec
    @param cpp_name_prefix: The C++ prefix to prepend to a message to refer to it (e.g. "std_msgs::")
    @type cpp_name_prefix: str
    """
    (cpp_msg_unqualified, cpp_msg_with_alloc, _) = roscpp.msg_gen.cpp_message_declarations(cpp_name_prefix, spec.short_name)
    
    s.write("/* Auto-generated by genmsg_cpp for file %s */\n"%(file))
    s.write('#ifndef %s_BOOST_SERIALIZATION_%s_H\n'%(spec.package.upper(), spec.short_name.upper()))
    s.write('#define %s_BOOST_SERIALIZATION_%s_H\n\n'%(spec.package.upper(), spec.short_name.upper()))
    s.write('#include <boost/serialization/serialization.hpp>\n')
    s.write('#include <boost/serialization/nvp.hpp>\n')
    s.write('#include <%s/%s.h>\n'%(spec.package,spec.short_name))
    write_boost_includes(s, spec)
    s.write('namespace boost\n{\n')
    s.write('namespace serialization\n{\n\n')
    
    s.write('template<class Archive, class ContainerAllocator>\n')

    s.write('void serialize(Archive& a, %s & m, unsigned int)\n{\n'%(cpp_msg_with_alloc))
    
    for field in spec.parsed_fields():
        s.write('    a & make_nvp("%s",m.%s);\n'%(field.name,field.name))
    s.write('}\n\n')
        
    s.write('} // namespace serialization\n')
    s.write('} // namespace boost\n\n')
    s.write('#endif // %s_BOOST_SERIALIZATION_%s_H\n'%(spec.package.upper(), spec.short_name.upper()))
    


def generate_boost_serialization(msg_path):
    """
    Generate a boost::serialization header
    
    @param msg_path: The path to the .msg file
    @type msg_path: str
    """
    (package_dir, package) = roslib.packages.get_dir_pkg(msg_path)
    (_, spec) = roslib.msgs.load_from_file(msg_path, package)
    cpp_prefix = '%s::'%(package)
    
    s = StringIO()
    write_boost_serialization(s, spec, cpp_prefix, msg_path)
    
    output_dir = 'include/%s/boost'%(package)
    try:
        os.makedirs(output_dir)
    except OSError, e:
        pass
         
    f = open('%s/%s.h'%(output_dir, spec.short_name), 'w')
    print >> f, s.getvalue()
    
    s.close()


def create_boost_headers(argv, stdout, stderr):
    from optparse import OptionParser
    parser = OptionParser(usage="usage: %prog [packages]", prog=NAME)
    (options, args) = parser.parse_args(argv)

    # get the file name
    if len(args) < 2:
        parser.error("you must specify at least package")
    pkgs = args[1:]
    for pkg in pkgs:
        pp = roslib.packages.get_pkg_dir(pkg);
        msgs = roslib.msgs.list_msg_types(pkg,False)
        for msg in msgs:
            generate_boost_serialization(pp+'/msg/'+msg+'.msg')

if __name__ == "__main__":
    try:
        create_boost_headers(sys.argv, sys.stdout, sys.stderr)
    except Exception, e:
        print >> sys.stderr, e
        sys.exit(1)
