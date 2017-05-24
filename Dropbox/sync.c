DIR *d;
int n;
struct dirent *f;
d = opendir(path_user);
if(d){
  while((f = readdir(d)) != NULL){
    if (f->d_type == DT_REG){
      struct stat attrib;
      sprintf(filename, "%s/%s", path_user, f->d_name);
      stat(filename, &attrib);
      strftime(time_modif, MAX_USERID, "%F %T", localtime(&attrib.st_mtime));
      package_updated(f->d_name, time_modif, buffer_write);
      write_str_to_socket(sock, buffer_write);
      n = read_until_eos(sock, buffer_read);
      if(n<0){
        exit(1);
      }
      //puts(buffer_read);
      char * val = strchr(buffer_read, ' ') + 1;
      char * mes = strchr(val, ' ');
      *(mes++) = '\0';
      int res_val = atoi(val);
      if (res_val == 2){
        // É diferente, virão infos
        puts(mes);
        n = read_until_eos(sock, buffer_read);
        puts(buffer_read);
        char * fname = strchr(buffer_read, '\"') + 1;
        char * mtime = strchr(  fname, '\"') + 2;
        *(mtime - 2) = '\0';
        char * fsptr = strchr(mtime, ' ') + 1;
        fsptr = strchr(fsptr, ' ') + 1;
        *(fsptr - 1) = '\0';
        int fsize = atoi(fsptr);
        int k = 0, r;
        char c;
        printf("%s %s %d", fname, mtime, fsize);
        while(k < fsize){
          r = read(sock, &c, 1);
          if (r < 0)
            break;

          printf("%c", c);
          k += r;
        }
      }
    }
  }
}
