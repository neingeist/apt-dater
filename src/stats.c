/* $Id$
 *
 */

#include <glib-2.0/glib/gstdio.h>

#include "apt-dater.h"
#include "exec.h"
#include "screen.h"
#include "stats.h"
#include "lock.h"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

gchar *getStatsFile(const gchar *hostname)
{
 GDir *dir;
 GError *error = NULL;
 const gchar *entry;
 GFileTest test;
 gchar *compare;
 gchar *statsfile = NULL;
 
 if(!(dir = g_dir_open(cfg->statsdir, 0, &error))) {
  g_error ("%s: %s", cfg->statsdir, error->message);
  return (NULL);
 }
 g_clear_error(&error);

 while(((entry = g_dir_read_name(dir)))) {
  compare = g_strdup_printf("%s.stat", hostname);
  if(!g_strcasecmp(entry, compare)) {
   statsfile = g_strdup_printf("%s/%s", cfg->statsdir, entry);

   test = G_FILE_TEST_IS_REGULAR | G_FILE_TEST_EXISTS;
   if(g_file_test (statsfile, test) == FALSE) {
    g_free(statsfile);
    statsfile = NULL;
   }
   
   g_free(compare);
   break;
  }
  g_free(compare);
 }

 g_dir_close(dir);

 return(statsfile);
}


gchar *getStatsFileName(const gchar *hostname)
{
 gchar *statsfile = NULL;

 if(hostname) {
  statsfile = g_strdup_printf("%s/%s.stat", cfg->statsdir, hostname);
 }

 return(statsfile);
}


gboolean removeStatsFile(const gchar *hostname)
{
 gboolean r = FALSE;
 gchar *statsfile;

 if(hostname) {
  statsfile = getStatsFileName(hostname);

  if(!g_unlink(statsfile)) r = TRUE;

  g_free(statsfile);  
 }

 return(r);

}


void refreshStatsOfNode(gpointer n)
{
 getUpdatesFromStat(((HostNode *) n));

 unsetLockForHost((HostNode *) n);

 rebuilddl = TRUE; /* Trigger a DrawList rebuild */
}


gboolean setStatsFileFromIOC(GIOChannel *ioc, GIOCondition condition,
			     gpointer n)
{
 gchar *statsfile = NULL;
 gchar *buf = NULL;
 GError *error = NULL;
 gsize bytes;
 gboolean r = TRUE;
 FILE *fp = NULL;
 GIOStatus iostatus =  G_IO_STATUS_NORMAL;

 if(!n) return(FALSE);

 statsfile = getStatsFileName(((HostNode *) n)->hostname);

 if (condition & (G_IO_HUP | G_IO_PRI | G_IO_IN))
  {
   buf = (gchar *) g_malloc0 (g_io_channel_get_buffer_size (ioc) + 1);

   r = FALSE;
   fp = fopen(statsfile, "a");

   while(iostatus == G_IO_STATUS_NORMAL && fp) {
    iostatus = g_io_channel_read_chars (ioc, buf,
					g_io_channel_get_buffer_size (ioc), 
					&bytes, NULL);

    if(iostatus == G_IO_STATUS_ERROR || iostatus == G_IO_STATUS_AGAIN)
     break;

    fwrite(buf, sizeof(gchar), bytes, fp);
    fflush(fp);

    r = TRUE;
   }

   if(r == TRUE) fclose(fp);

   g_free(buf);
  }


 if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL))
  {
   g_io_channel_unref (ioc);
   if(g_io_channel_shutdown (ioc, TRUE, &error) == G_IO_STATUS_ERROR) {
    g_warning("%s", error->message);
    g_clear_error (&error);
   }

   r = FALSE;
  }

 g_free(statsfile);

 return(r);
}


static gint cmpUpdates(gconstpointer a, gconstpointer b) {
    return strcmp(((UpdNode *)a)->package, ((UpdNode *)b)->package);
}

gboolean getUpdatesFromStat(HostNode *n)
{
 gchar *statsfile;
 char line[STATS_MAX_LINE_LEN];
 char buf[256];
 FILE  *fp;
 UpdNode *updnode = NULL;
 gint status, i;
 gchar **argv = NULL;

 if(!n) return (FALSE);

 if(!(statsfile = getStatsFile(n->hostname))) {
  n->category = C_NO_STATS;
  return (TRUE);
 }

 if(!(fp = (FILE *) g_fopen(statsfile, "r"))) {
  n->category = C_UNKNOW;
  g_free(statsfile);
  return (TRUE);
 }

 n->status = 0;

 if(n->updates) {
    g_list_free(n->updates);
    n->updates = NULL;
 }
 if(n->lsb_distributor) {
  g_free(n->lsb_distributor);
  n->lsb_distributor = NULL;
 }
 if(n->lsb_release) {
  g_free(n->lsb_release);
  n->lsb_release = NULL;
 }
 if(n->lsb_codename) {
  g_free(n->lsb_codename);
  n->lsb_codename = NULL;
 }
 if(n->kernelrel) {
  g_free(n->kernelrel);
  n->kernelrel = NULL;
 }

 int linesok = 0;
 while(fgets(line, STATS_MAX_LINE_LEN, fp)) {
  line[strlen(line) - 1] = 0;

  if (sscanf((gchar *) line, "KERNELINFO: %d %255s", &status, buf)) {
   n->kernelrel = g_strdup(buf);
   switch(status){
   case 1:
    n->status = n->status | HOST_STATUS_KERNELNOTMATCH;
    break;
   case 2:
    n->status = n->status | HOST_STATUS_KERNELSELFBUILD;
    break;
   }
   linesok++;
   continue;
  }
  
  if(!strncmp("STATUS: ", line, 8)) {
    argv = g_strsplit(&line[8], "|", 0);

    if(!argv) continue;

    i=0;
    while(argv[i]) i++;
    
    /* ignore invalid lines */
    if(i < 3) {
     g_strfreev(argv);
     continue;     
    }
    
    switch(argv[2][0]) {
	case 'u':
	    updnode = g_new0(UpdNode, 1);
	    updnode->package = g_strdup(argv[0]);
	    updnode->oldver = g_strdup(argv[1]);
	    if (strlen(argv[2]) > 3)
		updnode->newver = g_strdup(&argv[2][2]);
	    else
		updnode->newver = g_strdup("?");

	    n->updates = g_list_insert_sorted(n->updates, updnode, cmpUpdates);

	    break;
	case 'h':
	    n->status = n->status | HOST_STATUS_PKGKEPTBACK;
	    break;
	case 'x':
	    n->status = n->status | HOST_STATUS_PKGEXTRA;
	    break;
    }
    g_strfreev(argv);

    linesok++;
    continue;
  }

  if(!strncmp("LSBREL: ", line, 8)) {
   argv = g_strsplit(&line[8], "|", 0);

   if(!argv) continue;
   
   i=0;
   while(argv[i]) i++;
   
   /* ignore invalid lines */
   if(i < 3) {
    g_strfreev(argv);
    continue;     
   }

   if(strlen(argv[0]) > 0) {
    n->lsb_distributor = g_strdup(argv[0]);
    n->lsb_release = g_strdup(argv[1]);
    n->lsb_codename = g_strdup(argv[2]);
   }

   g_strfreev(argv);

   linesok++;
   continue;     
  }
 }

 if(linesok>5) {
   if(g_list_length(n->updates))
    n->category = C_UPDATES_PENDING;
   else
    n->category = C_UP_TO_DATE;
 }
 else
    n->category = C_UNKNOW;
  
 fclose(fp);
 g_free(statsfile);

 return(TRUE);
}


void freeUpdNode(UpdNode *n)
{
 if(n) {
  g_free(n->package);
  g_free(n->oldver);
  g_free(n->newver);
  g_free(n);
 }
}


void freeUpdates(GList *updates)
{
 if(updates) {
  g_list_foreach (updates, (GFunc) freeUpdNode, NULL);
  g_list_free(updates);
 }
}


gboolean refreshStats(GList *hosts)
{
 GList *ho;
 gboolean r = TRUE;

 ho = g_list_first(hosts);
 
 while(ho) {
  HostNode *n = (HostNode *) ho->data;

  if(screen_get_sessions(n)) {
   n->category = C_SESSIONS;
   rebuilddl = TRUE;
  }
  else {
   if (n->category == C_SESSIONS)
    n->category = C_REFRESH_REQUIRED;

   if(n->category == C_REFRESH_REQUIRED) {
    /* Try to get a lock for the host. */
    gint rsetlck = setLockForHost(n);

    /* We don't got the lock. */
    if(rsetlck == -1) {
     n->status|= HOST_STATUS_LOCKED;
     if(n->updates) {
      freeUpdates(n->updates);
      n->updates = NULL;
     }
    }
    /* We got the lock. */
    else if (rsetlck == 0) {
     if(n->status & HOST_STATUS_LOCKED) {
      refreshStatsOfNode(n);
      //      n->status ^= HOST_STATUS_LOCKED;
     } else {
      n->category = C_REFRESH;
      rebuilddl = TRUE;

      freeUpdates(n->updates);
      n->updates = NULL;

      if(ssh_cmd_refresh(n) == FALSE) {
       n->category = C_NO_STATS;
      }
     }
    /* Something weird happend. */
    } else {
     n->category = C_UNKNOW;
     rebuilddl = TRUE;
    }
   }
  }

  ho = g_list_next(ho);
 }

 return(r);
}


void getOldestMtime(GList *hosts)
{
 GList *ho;
 gchar *statsfile;
 struct stat stat_p;
 HostNode *n;

 oldest_st_mtime = time(NULL);

 ho = g_list_first(hosts);
 
 while(ho) {
  n = (HostNode *) ho->data;

  if((statsfile = getStatsFile(n->hostname)))
   {
    if(!stat(statsfile, &stat_p)) {
     if(difftime(stat_p.st_mtime, oldest_st_mtime) < 0)
      oldest_st_mtime = stat_p.st_mtime;
    }

    g_free(statsfile);
   }

  ho = g_list_next(ho);
 }
}
